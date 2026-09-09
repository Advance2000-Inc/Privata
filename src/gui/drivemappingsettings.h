#pragma once

#include <QWidget>

#ifdef Q_OS_WIN
#include "drivemappingmanager.h"

#include <QPoint>
#endif

class QTableWidget;
class QPushButton;

namespace OCC {

class AccountState;
class Folder;

/**
 * @brief Per-account settings tab letting the user map synced folders to drive letters.
 * @ingroup gui
 */
class DriveMappingSettings : public QWidget
{
    Q_OBJECT
public:
    explicit DriveMappingSettings(AccountState *accountState, QWidget *parent = nullptr);

private slots:
    void refresh();
    void slotForceRefresh();

private:
#ifdef Q_OS_WIN
    void buildRow(Folder *folder, int row);
    void buildManualRow(const DriveMappingManager::ManualMapping &mapping, int row);
    void buildPolicyRow(const DriveMappingManager::PolicyMapping &mapping, int row);
    void slotLetterChanged(Folder *folder, QChar letter);
    void slotManualLetterChanged(const QString &localPath, QChar letter);
    void slotAddManualMapping();
    void slotRemoveManualMapping(const QString &localPath);
    void slotRemoveSuggestedPolicyMapping(const QString &folderId, QChar letter);
    void slotShowContextMenu(const QPoint &pos);
    void slotMappingFailed(const QString &folderAlias, const QString &message);
    void slotRemoveAll();

    QTableWidget *_table = nullptr;
    QPushButton *_removeAllButton = nullptr;
#endif
    AccountState *_accountState;
};

} // namespace OCC
