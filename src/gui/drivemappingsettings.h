#pragma once

#include <QWidget>

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

private:
#ifdef Q_OS_WIN
    void buildRow(Folder *folder, int row);
    void slotLetterChanged(Folder *folder, QChar letter);
    void slotMappingFailed(const QString &folderAlias, const QString &message);
    void slotRemoveAll();

    QTableWidget *_table = nullptr;
    QPushButton *_removeAllButton = nullptr;
#endif
    AccountState *_accountState;
};

} // namespace OCC
