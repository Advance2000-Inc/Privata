#pragma once

#include <QChar>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVector>

namespace OCC {

class FolderMan;
class Folder;
class AccountState;
class JsonApiJob;

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
    struct ManualMapping
    {
        QString localPath;
        QChar driveLetter;
    };

    struct PolicyMapping
    {
        QString folderId;
        QString folderPath;
        QString relativePathHint;
        QString localPath;
        QChar driveLetter;
        QString enforcement;
        QString status;
        bool resolved = false;
        bool suppressed = false;
    };

    explicit DriveMappingManager(FolderMan *folderMan);

    /// Drive letters not currently used by a real drive, an existing substitution, or the system drive.
    [[nodiscard]] static QVector<QChar> availableDriveLetters();

    /// Creates or updates the substitution so that letter points at folder's local path.
    bool mapFolder(Folder *folder, QChar letter);

    /// Removes the substitution for letter, but only if this manager created it.
    bool unmapLetter(QChar letter);

    [[nodiscard]] QVector<ManualMapping> manualMappings(AccountState *accountState) const;
    [[nodiscard]] QVector<PolicyMapping> policyMappings(AccountState *accountState) const;
    bool addManualMapping(AccountState *accountState, const QString &localPath);
    bool removeManualMapping(AccountState *accountState, const QString &localPath);
    bool setManualMappingDriveLetter(AccountState *accountState, const QString &localPath, QChar letter);
    bool removeSuggestedPolicyMapping(AccountState *accountState, const QString &folderId, QChar letter);

    /// Re-establishes all persisted mappings; called once after folders are loaded at startup.
    void applyAllMappings();

signals:
    /// Emitted when a mapping could not be created or removed; message is user-facing.
    void mappingFailed(const QString &folderAlias, const QString &message);
    void mappingsChanged();

private:
    void registerPolicyAccount(AccountState *accountState);
    void fetchPolicyMappings(AccountState *accountState);
    void applyCachedPolicyMappings(AccountState *accountState);
    void applyPolicyMappings(AccountState *accountState, QVector<PolicyMapping> mappings, const QString &source);
    void saveManualMappings(AccountState *accountState, const QVector<ManualMapping> &mappings) const;
    [[nodiscard]] QVector<PolicyMapping> cachedPolicyMappings(AccountState *accountState) const;
    [[nodiscard]] bool resolvePolicyMapping(AccountState *accountState, PolicyMapping *mapping) const;
    [[nodiscard]] static QString policyKey(const QString &folderId, QChar letter);
    bool mapPath(const QString &localPath, QChar letter, const QString &folderAlias = QString(), bool adoptExistingMapping = true);
    [[nodiscard]] static bool letterInUse(QChar letter);
    [[nodiscard]] static bool substitutionTargets(QChar letter, const QString &localPath);
    [[nodiscard]] static bool createSubstitution(QChar letter, const QString &localPath, QString *error);
    [[nodiscard]] static bool removeSubstitution(QChar letter, QString *error);

    FolderMan *_folderMan;
    /// Letters this manager created, mapped to the local path they point at.
    QHash<QChar, QString> _ownedMappings;
    QHash<AccountState *, QPointer<JsonApiJob>> _policyJobs;
    QSet<AccountState *> _registeredPolicyAccounts;
    QTimer _policyRefreshTimer;
};

} // namespace OCC
