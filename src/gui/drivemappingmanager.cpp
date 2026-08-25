#include "drivemappingmanager.h"

#include "accountmanager.h"
#include "accountstate.h"
#include "common/syncjournalfilerecord.h"
#include "folder.h"
#include "folderman.h"
#include "networkjobs.h"

#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSettings>
#include <QByteArray>
#include <QTextStream>

#include <windows.h>

#include <algorithm>

namespace OCC {

Q_LOGGING_CATEGORY(lcDriveMappingManager, "nextcloud.gui.drivemappingmanager", QtInfoMsg)

namespace {

constexpr auto manualMappingsGroupC = "ManualDriveMappings";
constexpr auto policyCacheGroupC = "PolicyDriveMappingCache";
constexpr auto policyOwnedMappingsGroupC = "PolicyDriveMappings";
constexpr auto endpointPathC = "/ocs/v2.php/apps/drive_mapping_policies/api/v1/mappings";
constexpr auto pathKeyC = "path";
constexpr auto driveLetterKeyC = "driveLetter";
constexpr auto folderIdKeyC = "folderId";
constexpr auto folderPathKeyC = "folderPath";
constexpr auto relativePathHintKeyC = "relativePathHint";
constexpr auto enforcementKeyC = "enforcement";
constexpr auto suppressedKeyC = "suppressed";
constexpr auto jsonKeyC = "json";
constexpr auto enforcedC = "enforced";
constexpr auto suggestedC = "suggested";
constexpr auto useMockPolicyMappingsC = false;

QString driveSpec(QChar letter)
{
    return QStringLiteral("%1:").arg(letter.toUpper());
}

QString canonicalPath(const QString &path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

QString mappingKeyForPath(const QString &path)
{
    return QString::fromUtf8(canonicalPath(path).toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString mappingKeyForPolicy(const QString &folderId, QChar letter)
{
    const auto key = QStringLiteral("%1|%2").arg(folderId, QString(letter.toUpper()));
    return QString::fromUtf8(key.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString relativeHintToLocalPath(Folder *folder, const QString &relativePathHint)
{
    if (!folder || relativePathHint.isEmpty())
        return QString();

    auto hint = QDir::fromNativeSeparators(relativePathHint);
    while (hint.startsWith(QLatin1Char('/')))
        hint.remove(0, 1);

    return canonicalPath(QDir(folder->path()).filePath(hint));
}

QVector<DriveMappingManager::PolicyMapping> parsePolicyMappings(const QJsonDocument &doc)
{
    QVector<DriveMappingManager::PolicyMapping> result;
    const auto data = doc.object().value(QLatin1String("ocs")).toObject().value(QLatin1String("data")).toObject();
    const auto mappings = data.value(QLatin1String("mappings")).toArray();
    result.reserve(mappings.size());
    for (const auto &mappingValue : mappings) {
        const auto mappingObject = mappingValue.toObject();
        DriveMappingManager::PolicyMapping mapping;
        const auto driveLetter = mappingObject.value(QLatin1String("drive_letter")).toString();
        mapping.driveLetter = driveLetter.isEmpty() ? QChar() : driveLetter.at(0).toUpper();
        const auto folderIdValue = mappingObject.value(QLatin1String("folder_id"));
        mapping.folderId = folderIdValue.isString()
            ? folderIdValue.toString()
            : QString::number(static_cast<qint64>(folderIdValue.toDouble()));
        mapping.folderPath = mappingObject.value(QLatin1String("folder_path")).toString();
        mapping.relativePathHint = mappingObject.value(QLatin1String("relative_path_hint")).toString();
        mapping.enforcement = mappingObject.value(QLatin1String("enforcement")).toString().toLower();
        if (!mapping.folderId.isEmpty() && !mapping.driveLetter.isNull())
            result.append(mapping);
    }
    return result;
}

void logPolicyMappingsJson(const QString &source, AccountState *accountState, const QJsonDocument &doc)
{
    const auto accountName = accountState && accountState->account()
        ? accountState->account()->displayName()
        : QStringLiteral("unknown account");
    const auto compactJson = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
    qCInfo(lcDriveMappingManager).noquote()
        << QStringLiteral("Incoming policy drive mapping JSON from %1 for %2: %3")
               .arg(source, accountName, compactJson);

    auto logDirectoryPath = QString();
    if (accountState) {
        const auto settings = accountState->settings();
        logDirectoryPath = QFileInfo(settings->fileName()).absoluteDir().filePath(QStringLiteral("logs"));
    }
    if (logDirectoryPath.isEmpty())
        logDirectoryPath = QDir::tempPath();

    QDir logDirectory(logDirectoryPath);
    if (!logDirectory.exists() && !logDirectory.mkpath(QStringLiteral("."))) {
        qCWarning(lcDriveMappingManager) << "Could not create policy drive mapping log directory" << logDirectoryPath;
        return;
    }

    QFile logFile(logDirectory.filePath(QStringLiteral("drive-mapping-policies.log")));
    if (!logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qCWarning(lcDriveMappingManager) << "Could not open policy drive mapping log file" << logFile.fileName() << logFile.errorString();
        return;
    }

    QTextStream stream(&logFile);
    stream << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
           << " source=" << source
           << " account=" << accountName
           << " json=" << compactJson
           << Qt::endl;
}

// Returns the raw device string QueryDosDevice reports for a drive letter, or an empty string.
QString queryDosDeviceTarget(QChar letter)
{
    wchar_t buffer[MAX_PATH];
    const auto spec = driveSpec(letter);
    if (QueryDosDeviceW(reinterpret_cast<const wchar_t *>(spec.utf16()), buffer, MAX_PATH) == 0)
        return QString();
    return QString::fromWCharArray(buffer);
}

}

DriveMappingManager::DriveMappingManager(FolderMan *folderMan)
    : QObject(folderMan)
    , _folderMan(folderMan)
{
    qCInfo(lcDriveMappingManager) << "Drive mapping manager constructed";

    const auto refreshPolicyAccounts = [this](const QString &reason) {
        const auto accounts = AccountManager::instance()->accounts();
        qCInfo(lcDriveMappingManager) << "Refreshing policy drive mappings" << reason << "accountCount" << accounts.size();
        for (const auto &account : accounts) {
            registerPolicyAccount(account.data());
            applyCachedPolicyMappings(account.data());
            fetchPolicyMappings(account.data());
        }
    };

    _policyRefreshTimer.setInterval(std::chrono::minutes(30));
    connect(&_policyRefreshTimer, &QTimer::timeout, this, [refreshPolicyAccounts] {
        refreshPolicyAccounts(QStringLiteral("timer"));
    });
    _policyRefreshTimer.start();

    QTimer::singleShot(0, this, [refreshPolicyAccounts] {
        refreshPolicyAccounts(QStringLiteral("startup"));
    });

    connect(AccountManager::instance(), &AccountManager::accountAdded, this, [this](AccountState *accountState) {
        qCInfo(lcDriveMappingManager) << "Policy drive mapping account added" << (accountState && accountState->account() ? accountState->account()->displayName() : QStringLiteral("unknown account"));
        registerPolicyAccount(accountState);
        applyCachedPolicyMappings(accountState);
        fetchPolicyMappings(accountState);
    });

    connect(_folderMan, &FolderMan::folderListChanged, this, [this, refreshPolicyAccounts] {
        QTimer::singleShot(0, this, [refreshPolicyAccounts] {
            refreshPolicyAccounts(QStringLiteral("folderListChanged"));
        });
    });
}

QVector<QChar> DriveMappingManager::availableDriveLetters()
{
    QVector<QChar> result;
    const DWORD usedMask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if ((usedMask & (1u << i)) == 0)
            result.append(QChar(QLatin1Char('A' + i)));
    }
    return result;
}

bool DriveMappingManager::letterInUse(QChar letter)
{
    const DWORD usedMask = GetLogicalDrives();
    const int index = letter.toUpper().toLatin1() - 'A';
    if (index < 0 || index >= 26)
        return true;
    return (usedMask & (1u << index)) != 0;
}

bool DriveMappingManager::substitutionTargets(QChar letter, const QString &localPath)
{
    const auto target = queryDosDeviceTarget(letter);
    if (!target.startsWith(QLatin1String("\\??\\")))
        return false;
    return canonicalPath(target.mid(4)) == canonicalPath(localPath);
}

bool DriveMappingManager::createSubstitution(QChar letter, const QString &localPath, QString *error)
{
    const auto spec = driveSpec(letter);
    const auto nativePath = QDir::toNativeSeparators(canonicalPath(localPath));
    if (!DefineDosDeviceW(0, reinterpret_cast<const wchar_t *>(spec.utf16()),
            reinterpret_cast<const wchar_t *>(nativePath.utf16()))) {
        if (error)
            *error = DriveMappingManager::tr("Could not create drive mapping %1 (error %2).").arg(spec).arg(GetLastError());
        return false;
    }
    return true;
}

bool DriveMappingManager::removeSubstitution(QChar letter, QString *error)
{
    const auto spec = driveSpec(letter);
    if (!DefineDosDeviceW(DDD_REMOVE_DEFINITION, reinterpret_cast<const wchar_t *>(spec.utf16()), nullptr)) {
        const auto lastError = GetLastError();
        if (lastError != ERROR_FILE_NOT_FOUND && lastError != ERROR_PATH_NOT_FOUND) {
            if (error)
                *error = DriveMappingManager::tr("Could not remove drive mapping %1 (error %2).").arg(spec).arg(lastError);
            return false;
        }
    }
    return true;
}

bool DriveMappingManager::mapFolder(Folder *folder, QChar letter)
{
    if (!folder || letter.isNull())
        return false;

    return mapPath(folder->path(), letter, folder->alias());
}

bool DriveMappingManager::mapPath(const QString &localPath, QChar letter, const QString &folderAlias)
{
    if (letter.isNull())
        return false;

    letter = letter.toUpper();
    const auto path = canonicalPath(localPath);

    // Already correctly mapped, whether by us in this session or from before: nothing to do.
    if (substitutionTargets(letter, path)) {
        _ownedMappings.insert(letter, path);
        return true;
    }

    if (letterInUse(letter)) {
        qCWarning(lcDriveMappingManager) << "Drive letter" << letter << "is already in use, refusing to overwrite it";
        emit mappingFailed(folderAlias, tr("Drive letter %1 is already in use by something else.").arg(driveSpec(letter)));
        return false;
    }

    QString error;
    if (!createSubstitution(letter, path, &error)) {
        qCWarning(lcDriveMappingManager) << "Failed to map" << letter << "to" << path << ":" << error;
        emit mappingFailed(folderAlias, error);
        return false;
    }

    _ownedMappings.insert(letter, path);
    emit mappingsChanged();
    return true;
}

QString DriveMappingManager::policyKey(const QString &folderId, QChar letter)
{
    return mappingKeyForPolicy(folderId, letter);
}

void DriveMappingManager::registerPolicyAccount(AccountState *accountState)
{
    if (!accountState || _registeredPolicyAccounts.contains(accountState))
        return;

    _registeredPolicyAccounts.insert(accountState);
    connect(accountState, &QObject::destroyed, this, [this, accountState] {
        _registeredPolicyAccounts.remove(accountState);
        _policyJobs.remove(accountState);
    });
    connect(accountState, &AccountState::isConnectedChanged, this, [this, accountState] {
        if (accountState->isConnected())
            fetchPolicyMappings(accountState);
        else
            applyCachedPolicyMappings(accountState);
    });
}

void DriveMappingManager::fetchPolicyMappings(AccountState *accountState)
{
    if (!accountState || !accountState->isConnected() || _policyJobs.value(accountState)) {
        qCInfo(lcDriveMappingManager) << "Skipping policy drive mapping fetch"
                                      << "hasAccountState" << static_cast<bool>(accountState)
                                      << "isConnected" << (accountState ? accountState->isConnected() : false)
                                      << "hasRunningJob" << static_cast<bool>(accountState ? _policyJobs.value(accountState) : nullptr);
        return;
    }

    if (useMockPolicyMappingsC) {
        const auto availableLetters = availableDriveLetters();
        if (availableLetters.isEmpty()) {
            qCWarning(lcDriveMappingManager) << "Mock policy drive mapping skipped because no drive letters are available for" << accountState->account()->displayName();
            return;
        }
/*
        const auto mockLetter = availableLetters.contains(QChar(QLatin1Char('G')))
            ? QChar(QLatin1Char('G'))
            : availableLetters.constFirst();
*/
        const auto mockLetter = QLatin1Char('Z');
        const auto mockJson = QString::fromLatin1(R"({
            "ocs": {
                "data": {
                    "version": 42,
                    "mappings": [
                        {
                            "drive_letter": "%1",
                            "folder_id": 12345,
                            "folder_path": "/ZZ",
                            "relative_path_hint": "ZZ",
                            "enforcement": "enforced"
                        }
                    ]
                }
            }
        })").arg(QString(mockLetter));

        QJsonParseError parseError;
        const auto doc = QJsonDocument::fromJson(mockJson.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || doc.isNull()) {
            qCWarning(lcDriveMappingManager) << "Mock policy drive mapping JSON is invalid:" << parseError.errorString();
            return;
        }
        logPolicyMappingsJson(QStringLiteral("mock"), accountState, doc);

        auto settings = accountState->settings();
        settings->beginGroup(QLatin1String(policyCacheGroupC));
        settings->setValue(QLatin1String(jsonKeyC), QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
        settings->endGroup();

        qCInfo(lcDriveMappingManager) << "Using mock policy drive mappings for" << accountState->account()->displayName() << "letter" << mockLetter;
        applyPolicyMappings(accountState, parsePolicyMappings(doc), QStringLiteral("mock"));
        return;
    }

    const QPointer<AccountState> guardedAccountState(accountState);

    qCInfo(lcDriveMappingManager) << "Fetching policy drive mappings for" << accountState->account()->displayName();
    auto *job = new JsonApiJob(accountState->account(), QLatin1String(endpointPathC), this);
    _policyJobs.insert(accountState, job);
    connect(job, &JsonApiJob::jsonReceived, this, [this, guardedAccountState, job](const QJsonDocument &doc, int statusCode) {
        _policyJobs.remove(guardedAccountState.data());
        if (!guardedAccountState) {
            job->deleteLater();
            return;
        }

        auto *accountState = guardedAccountState.data();

        if ((statusCode != 100 && statusCode != 200) || doc.isNull()) {
            qCWarning(lcDriveMappingManager) << "Policy drive mapping retrieval failed for" << accountState->account()->displayName() << "status" << statusCode;
            applyCachedPolicyMappings(accountState);
            job->deleteLater();
            return;
        }
        logPolicyMappingsJson(QStringLiteral("server"), accountState, doc);

        auto settings = accountState->settings();
        settings->beginGroup(QLatin1String(policyCacheGroupC));
        settings->setValue(QLatin1String(jsonKeyC), QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
        settings->endGroup();

        qCInfo(lcDriveMappingManager) << "Policy drive mapping retrieval succeeded for" << accountState->account()->displayName();
        applyPolicyMappings(accountState, parsePolicyMappings(doc), QStringLiteral("server"));
        job->deleteLater();
    });
    job->start();
}

QVector<DriveMappingManager::PolicyMapping> DriveMappingManager::cachedPolicyMappings(AccountState *accountState) const
{
    QVector<PolicyMapping> result;
    if (!accountState)
        return result;

    auto settings = accountState->settings();
    settings->beginGroup(QLatin1String(policyCacheGroupC));
    const auto json = settings->value(QLatin1String(jsonKeyC)).toString().toUtf8();
    settings->endGroup();
    if (json.isEmpty())
        return result;

    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || doc.isNull()) {
        qCWarning(lcDriveMappingManager) << "Cached policy drive mapping JSON is invalid:" << error.errorString();
        return result;
    }

    result = parsePolicyMappings(doc);
    for (auto &mapping : result) {
        const auto key = policyKey(mapping.folderId, mapping.driveLetter);
        settings->beginGroup(QLatin1String(policyOwnedMappingsGroupC));
        settings->beginGroup(key);
        mapping.suppressed = settings->value(QLatin1String(suppressedKeyC), false).toBool();
        mapping.localPath = settings->value(QLatin1String(pathKeyC)).toString();
        settings->endGroup();
        settings->endGroup();
    }
    return result;
}

QVector<DriveMappingManager::PolicyMapping> DriveMappingManager::policyMappings(AccountState *accountState) const
{
    auto mappings = cachedPolicyMappings(accountState);
    for (auto &mapping : mappings) {
        const auto resolved = resolvePolicyMapping(accountState, &mapping);
        Q_UNUSED(resolved)
    }
    return mappings;
}

void DriveMappingManager::applyCachedPolicyMappings(AccountState *accountState)
{
    applyPolicyMappings(accountState, cachedPolicyMappings(accountState), QStringLiteral("cache"));
}

bool DriveMappingManager::resolvePolicyMapping(AccountState *accountState, PolicyMapping *mapping) const
{
    if (!accountState || !mapping || mapping->folderId.isEmpty())
        return false;

    const auto folders = _folderMan->map().values();
    const auto folderId = mapping->folderId.toUtf8();
    for (auto *folder : folders) {
        if (folder->accountState() != accountState)
            continue;

        SyncJournalFileRecord matchedRecord;
        const auto foundByFileId = folder->journalDb()->getFileRecordsByFileId(folderId, [&matchedRecord](const SyncJournalFileRecord &record) {
            if (!matchedRecord.isValid() && record.isDirectory())
                matchedRecord = record;
        });
        if (!foundByFileId)
            qCWarning(lcDriveMappingManager) << "Failed to resolve policy drive mapping by file id" << mapping->folderId;

        if (!matchedRecord.isValid()) {
            const auto foundByNumericFileId = folder->journalDb()->getFilesBelowPath(QByteArray(), [&matchedRecord, &folderId](const SyncJournalFileRecord &record) {
                if (!matchedRecord.isValid() && record.isDirectory() && record.numericFileId() == folderId)
                    matchedRecord = record;
            });
            if (!foundByNumericFileId)
                qCWarning(lcDriveMappingManager) << "Failed to resolve policy drive mapping by numeric file id" << mapping->folderId;
        }

        if (matchedRecord.isValid()) {
            mapping->localPath = canonicalPath(QDir(folder->path()).filePath(matchedRecord.path()));
            mapping->resolved = true;
            mapping->status = tr("Resolved from folder id %1.").arg(mapping->folderId);
            return true;
        }
    }

    for (auto *folder : folders) {
        if (folder->accountState() != accountState)
            continue;
        const auto hintedPath = relativeHintToLocalPath(folder, mapping->relativePathHint);
        if (hintedPath.isEmpty())
            continue;
        mapping->localPath = hintedPath;
        mapping->resolved = QDir(hintedPath).exists();
        mapping->status = mapping->resolved
            ? tr("Resolved from relative path hint %1.").arg(mapping->relativePathHint)
            : tr("The policy target %1 is excluded from synchronization or is not present locally.").arg(mapping->folderPath);
        return mapping->resolved;
    }

    mapping->status = tr("No configured folder can resolve policy target %1.").arg(mapping->folderPath);
    return false;
}

void DriveMappingManager::applyPolicyMappings(AccountState *accountState, QVector<PolicyMapping> mappings, const QString &source)
{
    if (!accountState)
        return;

    QSet<QString> currentKeys;
    auto settings = accountState->settings();
    settings->beginGroup(QLatin1String(policyOwnedMappingsGroupC));

    for (auto &mapping : mappings) {
        const auto key = policyKey(mapping.folderId, mapping.driveLetter);
        currentKeys.insert(key);
        settings->beginGroup(key);
        const auto previousPath = settings->value(QLatin1String(pathKeyC)).toString();
        const auto wasSuppressed = settings->value(QLatin1String(suppressedKeyC), false).toBool();
        settings->endGroup();

        mapping.suppressed = wasSuppressed;
        if (!resolvePolicyMapping(accountState, &mapping)) {
            qCWarning(lcDriveMappingManager) << "Policy drive mapping" << mapping.driveLetter << mapping.folderId << "not applied:" << mapping.status;
            emit mappingFailed(mapping.folderPath, mapping.status);
            continue;
        }

        const auto enforcement = mapping.enforcement.isEmpty() ? QString::fromLatin1(suggestedC) : mapping.enforcement;
        const auto currentlyTargets = substitutionTargets(mapping.driveLetter, mapping.localPath);
        if (enforcement == QLatin1String(suggestedC) && wasSuppressed && !currentlyTargets) {
            qCInfo(lcDriveMappingManager) << "Suggested policy drive mapping" << mapping.driveLetter << mapping.folderId << "remains suppressed";
            continue;
        }
        if (enforcement == QLatin1String(suggestedC) && !previousPath.isEmpty() && !currentlyTargets && !letterInUse(mapping.driveLetter)) {
            settings->beginGroup(key);
            settings->setValue(QLatin1String(suppressedKeyC), true);
            settings->endGroup();
            qCInfo(lcDriveMappingManager) << "Suggested policy drive mapping" << mapping.driveLetter << mapping.folderId << "was removed by the user and will stay removed";
            continue;
        }

        qCInfo(lcDriveMappingManager) << "Applying" << enforcement << "policy drive mapping" << mapping.driveLetter << mapping.folderId << "from" << source << "to" << mapping.localPath;
        if (!mapPath(mapping.localPath, mapping.driveLetter, mapping.folderPath))
            continue;

        settings->beginGroup(key);
        settings->setValue(QLatin1String(folderIdKeyC), mapping.folderId);
        settings->setValue(QLatin1String(folderPathKeyC), mapping.folderPath);
        settings->setValue(QLatin1String(relativePathHintKeyC), mapping.relativePathHint);
        settings->setValue(QLatin1String(pathKeyC), mapping.localPath);
        settings->setValue(QLatin1String(driveLetterKeyC), QString(mapping.driveLetter));
        settings->setValue(QLatin1String(enforcementKeyC), enforcement);
        settings->setValue(QLatin1String(suppressedKeyC), false);
        settings->endGroup();
    }

    const auto ownedGroups = settings->childGroups();
    for (const auto &ownedGroup : ownedGroups) {
        if (currentKeys.contains(ownedGroup))
            continue;

        settings->beginGroup(ownedGroup);
        const auto oldPath = settings->value(QLatin1String(pathKeyC)).toString();
        const auto oldLetterString = settings->value(QLatin1String(driveLetterKeyC)).toString();
        settings->endGroup();
        const auto oldLetter = oldLetterString.isEmpty() ? QChar() : oldLetterString.at(0).toUpper();
        if (!oldLetter.isNull() && !oldPath.isEmpty() && substitutionTargets(oldLetter, oldPath)) {
            qCInfo(lcDriveMappingManager) << "Removing obsolete policy drive mapping" << oldLetter << oldPath;
            _ownedMappings.insert(oldLetter, canonicalPath(oldPath));
            unmapLetter(oldLetter);
        }
        settings->remove(ownedGroup);
    }

    settings->endGroup();
    emit mappingsChanged();
}

bool DriveMappingManager::unmapLetter(QChar letter)
{
    if (letter.isNull())
        return true;

    letter = letter.toUpper();
    // Only remove a mapping we actually created; never touch someone else's drive.
    if (!_ownedMappings.contains(letter))
        return true;

    QString error;
    if (!removeSubstitution(letter, &error)) {
        qCWarning(lcDriveMappingManager) << "Failed to remove mapping for" << letter << ":" << error;
        emit mappingFailed(QString(), error);
        return false;
    }

    _ownedMappings.remove(letter);
    emit mappingsChanged();
    return true;
}

QVector<DriveMappingManager::ManualMapping> DriveMappingManager::manualMappings(AccountState *accountState) const
{
    QVector<ManualMapping> result;
    if (!accountState)
        return result;

    auto settings = accountState->settings();
    settings->beginGroup(QLatin1String(manualMappingsGroupC));
    const auto mappingGroups = settings->childGroups();
    result.reserve(mappingGroups.size());
    for (const auto &mappingGroup : mappingGroups) {
        settings->beginGroup(mappingGroup);
        const auto localPath = settings->value(QLatin1String(pathKeyC)).toString();
        const auto driveLetterString = settings->value(QLatin1String(driveLetterKeyC)).toString();
        settings->endGroup();

        if (localPath.isEmpty())
            continue;

        result.append({ canonicalPath(localPath), driveLetterString.isEmpty() ? QChar() : driveLetterString.at(0).toUpper() });
    }
    settings->endGroup();
    return result;
}

void DriveMappingManager::saveManualMappings(AccountState *accountState, const QVector<ManualMapping> &mappings) const
{
    if (!accountState)
        return;

    auto settings = accountState->settings();
    settings->beginGroup(QLatin1String(manualMappingsGroupC));
    settings->remove(QString());
    for (const auto &mapping : mappings) {
        settings->beginGroup(mappingKeyForPath(mapping.localPath));
        settings->setValue(QLatin1String(pathKeyC), canonicalPath(mapping.localPath));
        if (!mapping.driveLetter.isNull())
            settings->setValue(QLatin1String(driveLetterKeyC), QString(mapping.driveLetter.toUpper()));
        settings->endGroup();
    }
    settings->endGroup();
}

bool DriveMappingManager::addManualMapping(AccountState *accountState, const QString &localPath)
{
    const auto path = canonicalPath(localPath);
    if (!accountState || path.isEmpty())
        return false;

    auto mappings = manualMappings(accountState);
    const auto existing = std::find_if(mappings.cbegin(), mappings.cend(), [&path](const ManualMapping &mapping) {
        return canonicalPath(mapping.localPath) == path;
    });
    if (existing != mappings.cend())
        return false;

    mappings.append({ path, QChar() });
    saveManualMappings(accountState, mappings);
    emit mappingsChanged();
    return true;
}

bool DriveMappingManager::removeManualMapping(AccountState *accountState, const QString &localPath)
{
    const auto path = canonicalPath(localPath);
    auto mappings = manualMappings(accountState);
    const auto oldSize = mappings.size();
    auto removedLetter = QChar();
    mappings.erase(std::remove_if(mappings.begin(), mappings.end(), [&path, &removedLetter](const ManualMapping &mapping) {
        if (canonicalPath(mapping.localPath) != path)
            return false;
        removedLetter = mapping.driveLetter;
        return true;
    }), mappings.end());

    if (mappings.size() == oldSize)
        return false;

    if (!removedLetter.isNull())
        unmapLetter(removedLetter);
    saveManualMappings(accountState, mappings);
    emit mappingsChanged();
    return true;
}

bool DriveMappingManager::setManualMappingDriveLetter(AccountState *accountState, const QString &localPath, QChar letter)
{
    const auto path = canonicalPath(localPath);
    if (!letter.isNull())
        letter = letter.toUpper();

    auto mappings = manualMappings(accountState);
    auto mapping = std::find_if(mappings.begin(), mappings.end(), [&path](const ManualMapping &candidate) {
        return canonicalPath(candidate.localPath) == path;
    });
    if (mapping == mappings.end() || mapping->driveLetter == letter)
        return false;

    const auto previousLetter = mapping->driveLetter;
    if (!previousLetter.isNull())
        unmapLetter(previousLetter);

    if (!letter.isNull()) {
        if (!mapPath(path, letter))
            return false;
    }

    mapping->driveLetter = letter;
    saveManualMappings(accountState, mappings);
    emit mappingsChanged();
    return true;
}

bool DriveMappingManager::removeSuggestedPolicyMapping(AccountState *accountState, const QString &folderId, QChar letter)
{
    if (!accountState || folderId.isEmpty() || letter.isNull())
        return false;

    letter = letter.toUpper();
    const auto key = policyKey(folderId, letter);
    auto settings = accountState->settings();
    settings->beginGroup(QLatin1String(policyOwnedMappingsGroupC));
    settings->beginGroup(key);
    const auto enforcement = settings->value(QLatin1String(enforcementKeyC), QLatin1String(suggestedC)).toString();
    const auto localPath = settings->value(QLatin1String(pathKeyC)).toString();
    if (enforcement == QLatin1String(enforcedC)) {
        settings->endGroup();
        settings->endGroup();
        emit mappingFailed(QString(), tr("This drive mapping is enforced by administrator policy and cannot be removed from the client."));
        return false;
    }

    settings->setValue(QLatin1String(suppressedKeyC), true);
    settings->endGroup();
    settings->endGroup();

    if (!localPath.isEmpty() && substitutionTargets(letter, localPath)) {
        _ownedMappings.insert(letter, canonicalPath(localPath));
        unmapLetter(letter);
    }
    emit mappingsChanged();
    return true;
}

void DriveMappingManager::applyAllMappings()
{
    if (!_folderMan) {
        qCInfo(lcDriveMappingManager) << "Skipping all drive mappings because FolderMan is not available";
        return;
    }

    const auto accounts = AccountManager::instance()->accounts();
    const auto folders = _folderMan->map();
    qCInfo(lcDriveMappingManager) << "Applying all drive mappings"
                                  << "accountCount" << accounts.size()
                                  << "folderCount" << folders.size();
    for (auto *folder : folders) {
        if (!folder->driveLetter().isNull())
            mapFolder(folder, folder->driveLetter());
    }

    for (const auto &account : accounts) {
        const auto mappings = manualMappings(account.data());
        for (const auto &mapping : mappings) {
            if (!mapping.driveLetter.isNull())
                mapPath(mapping.localPath, mapping.driveLetter);
        }
    }

    for (const auto &account : accounts) {
        registerPolicyAccount(account.data());
        applyCachedPolicyMappings(account.data());
        fetchPolicyMappings(account.data());
    }
}

} // namespace OCC
