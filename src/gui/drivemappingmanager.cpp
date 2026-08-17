#include "drivemappingmanager.h"

#include "folder.h"
#include "folderman.h"

#include <QDir>
#include <QLoggingCategory>

#include <windows.h>

namespace OCC {

Q_LOGGING_CATEGORY(lcDriveMappingManager, "nextcloud.gui.drivemappingmanager", QtInfoMsg)

namespace {

QString driveSpec(QChar letter)
{
    return QStringLiteral("%1:").arg(letter.toUpper());
}

QString canonicalPath(const QString &path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
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

    letter = letter.toUpper();
    const auto localPath = canonicalPath(folder->path());

    // Already correctly mapped, whether by us in this session or from before: nothing to do.
    if (substitutionTargets(letter, localPath)) {
        _ownedMappings.insert(letter, localPath);
        return true;
    }

    if (letterInUse(letter)) {
        qCWarning(lcDriveMappingManager) << "Drive letter" << letter << "is already in use, refusing to overwrite it";
        emit mappingFailed(folder->alias(), tr("Drive letter %1 is already in use by something else.").arg(driveSpec(letter)));
        return false;
    }

    QString error;
    if (!createSubstitution(letter, localPath, &error)) {
        qCWarning(lcDriveMappingManager) << "Failed to map" << letter << "to" << localPath << ":" << error;
        emit mappingFailed(folder->alias(), error);
        return false;
    }

    _ownedMappings.insert(letter, localPath);
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

void DriveMappingManager::applyAllMappings()
{
    if (!_folderMan)
        return;

    const auto folders = _folderMan->map();
    for (auto *folder : folders) {
        if (!folder->driveLetter().isNull())
            mapFolder(folder, folder->driveLetter());
    }
}

} // namespace OCC
