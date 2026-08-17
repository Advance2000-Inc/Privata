#pragma once

#include <QChar>
#include <QHash>
#include <QObject>
#include <QVector>

namespace OCC {

class FolderMan;
class Folder;

/**
 * @brief Maps synced folders to Windows drive letters.
 * @ingroup gui
 *
 * Uses DefineDosDevice/QueryDosDevice directly instead of shelling out to subst.exe,
 * so no console window ever appears and no external process is spawned.
 */
class DriveMappingManager : public QObject
{
    Q_OBJECT
public:
    explicit DriveMappingManager(FolderMan *folderMan);

    /// Drive letters not currently used by a real drive, an existing substitution, or the system drive.
    [[nodiscard]] static QVector<QChar> availableDriveLetters();

    /// Creates or updates the substitution so that letter points at folder's local path.
    bool mapFolder(Folder *folder, QChar letter);

    /// Removes the substitution for letter, but only if this manager created it.
    bool unmapLetter(QChar letter);

    /// Re-establishes all persisted mappings; called once after folders are loaded at startup.
    void applyAllMappings();

signals:
    /// Emitted when a mapping could not be created or removed; message is user-facing.
    void mappingFailed(const QString &folderAlias, const QString &message);
    void mappingsChanged();

private:
    [[nodiscard]] static bool letterInUse(QChar letter);
    [[nodiscard]] static bool substitutionTargets(QChar letter, const QString &localPath);
    [[nodiscard]] static bool createSubstitution(QChar letter, const QString &localPath, QString *error);
    [[nodiscard]] static bool removeSubstitution(QChar letter, QString *error);

    FolderMan *_folderMan;
    /// Letters this manager created, mapped to the local path they point at.
    QHash<QChar, QString> _ownedMappings;
};

} // namespace OCC
