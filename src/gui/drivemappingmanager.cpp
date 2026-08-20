#include "drivemappingmanager.h"

#include "accountmanager.h"
#include "accountstate.h"
#include "folder.h"
#include "folderman.h"

#include <QDir>
#include <QLoggingCategory>
#include <QSettings>
#include <QByteArray>

#include <windows.h>

#include <algorithm>

namespace OCC {

Q_LOGGING_CATEGORY(lcDriveMappingManager, "nextcloud.gui.drivemappingmanager", QtInfoMsg)

namespace {

constexpr auto manualMappingsGroupC = "ManualDriveMappings";
constexpr auto pathKeyC = "path";
constexpr auto driveLetterKeyC = "driveLetter";

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

void DriveMappingManager::applyAllMappings()
{
    if (!_folderMan)
        return;

    const auto folders = _folderMan->map();
    for (auto *folder : folders) {
        if (!folder->driveLetter().isNull())
            mapFolder(folder, folder->driveLetter());
    }

    const auto accounts = AccountManager::instance()->accounts();
    for (const auto &account : accounts) {
        const auto mappings = manualMappings(account.data());
        for (const auto &mapping : mappings) {
            if (!mapping.driveLetter.isNull())
                mapPath(mapping.localPath, mapping.driveLetter);
        }
    }
}

} // namespace OCC
