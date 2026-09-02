#include "drivemappingsettings.h"

#include "accountstate.h"
#include "folder.h"
#include "folderman.h"

#include <QLabel>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include "drivemappingmanager.h"

#include <QComboBox>
#include <QColor>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>

#include <algorithm>

namespace {
constexpr int rowKindRole = Qt::UserRole + 1;
constexpr int driveLetterRole = Qt::UserRole + 2;
constexpr int enforcementRole = Qt::UserRole + 3;
constexpr auto manualRowKindC = "manual";
constexpr auto policyRowKindC = "policy";
constexpr auto enforcedC = "enforced";
}
#endif

namespace OCC {

DriveMappingSettings::DriveMappingSettings(AccountState *accountState, QWidget *parent)
    : QWidget(parent)
    , _accountState(accountState)
{
    auto *layout = new QVBoxLayout(this);

#ifndef Q_OS_WIN
    auto *label = new QLabel(tr("Drive letter mapping is only available on Windows."), this);
    label->setWordWrap(true);
    layout->addWidget(label);
#else
    auto *toolbarLayout = new QHBoxLayout;

    auto *addButton = new QPushButton(tr("Add"), this);
    connect(addButton, &QPushButton::clicked, this, &DriveMappingSettings::slotAddManualMapping);
    toolbarLayout->addWidget(addButton);

    auto *refreshButton = new QPushButton(tr("Refresh"), this);
    connect(refreshButton, &QPushButton::clicked, this, &DriveMappingSettings::slotForceRefresh);
    toolbarLayout->addWidget(refreshButton);

    _removeAllButton = new QPushButton(tr("Remove all mappings"), this);
    connect(_removeAllButton, &QPushButton::clicked, this, &DriveMappingSettings::slotRemoveAll);
    toolbarLayout->addWidget(_removeAllButton);
    toolbarLayout->addStretch();
    layout->addLayout(toolbarLayout);

    _table = new QTableWidget(this);
    _table->setColumnCount(4);
    _table->setHorizontalHeaderLabels({ tr("Folder"), tr("Local path"), tr("Type"), tr("Drive") });
    _table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    _table->verticalHeader()->setVisible(false);
    _table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _table->setSelectionMode(QAbstractItemView::NoSelection);
    _table->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(_table);

    connect(_table, &QTableWidget::customContextMenuRequested, this, &DriveMappingSettings::slotShowContextMenu);

    connect(FolderMan::instance(), &FolderMan::folderListChanged, this, &DriveMappingSettings::refresh);
    connect(&FolderMan::instance()->driveMappingManager(), &DriveMappingManager::mappingFailed,
        this, &DriveMappingSettings::slotMappingFailed);
    connect(&FolderMan::instance()->driveMappingManager(), &DriveMappingManager::mappingsChanged,
        this, &DriveMappingSettings::refresh);

    refresh();
    // Pick up any policy changes made on the server since this tab was last shown, rather than the stale cache.
    FolderMan::instance()->driveMappingManager().refreshPolicyMappings(_accountState);
#endif
}

void DriveMappingSettings::slotForceRefresh()
{
#ifdef Q_OS_WIN
    FolderMan::instance()->driveMappingManager().refreshPolicyMappings(_accountState);
#endif
    refresh();
}

void DriveMappingSettings::refresh()
{
#ifdef Q_OS_WIN
    _table->setRowCount(0);
    const auto folders = FolderMan::instance()->map().values();
    int row = 0;
    bool anyMapped = false;
    for (auto *folder : folders) {
        if (folder->accountState() != _accountState)
            continue;
        _table->insertRow(row);
        buildRow(folder, row);
        anyMapped = anyMapped || !folder->driveLetter().isNull();
        ++row;
    }
    const auto policyMappings = FolderMan::instance()->driveMappingManager().policyMappings(_accountState);
    for (const auto &mapping : policyMappings) {
        _table->insertRow(row);
        buildPolicyRow(mapping, row);
        ++row;
    }
    const auto manualMappings = FolderMan::instance()->driveMappingManager().manualMappings(_accountState);
    for (const auto &mapping : manualMappings) {
        _table->insertRow(row);
        buildManualRow(mapping, row);
        anyMapped = anyMapped || !mapping.driveLetter.isNull();
        ++row;
    }
    _removeAllButton->setEnabled(anyMapped);
#endif
}

#ifdef Q_OS_WIN

void DriveMappingSettings::buildRow(Folder *folder, int row)
{
    _table->setItem(row, 0, new QTableWidgetItem(folder->shortGuiRemotePathOrAppName()));
    _table->setItem(row, 1, new QTableWidgetItem(QDir::toNativeSeparators(folder->path())));
    _table->setItem(row, 2, new QTableWidgetItem(tr("Synced folder")));
    const auto currentLetter = folder->driveLetter();

    auto *combo = new QComboBox(this);
    combo->addItem(tr("No drive"), QVariant::fromValue(QChar()));
    auto letters = DriveMappingManager::availableDriveLetters();
    if (!currentLetter.isNull() && !letters.contains(currentLetter))
        letters.append(currentLetter);
    std::sort(letters.begin(), letters.end());
    for (auto letter : std::as_const(letters))
        combo->addItem(QStringLiteral("%1:").arg(letter), QVariant::fromValue(letter));
    combo->setCurrentIndex(currentLetter.isNull() ? 0 : combo->findData(QVariant::fromValue(currentLetter)));
    connect(combo, &QComboBox::currentIndexChanged, this, [this, folder, combo](int index) {
        slotLetterChanged(folder, combo->itemData(index).toChar());
    });
    _table->setCellWidget(row, 3, combo);
}

QString extractFolderNameFromPath(const QString &path)
{
    const auto cleanPath = QDir::cleanPath(path);
    const auto lastSlash = cleanPath.lastIndexOf(QLatin1Char('/'));
    if (lastSlash < 0)
        return cleanPath;
    return cleanPath.mid(lastSlash + 1);
}

void DriveMappingSettings::buildManualRow(const DriveMappingManager::ManualMapping &mapping, int row)
{
    _table->setItem(row, 0, new QTableWidgetItem(extractFolderNameFromPath(mapping.localPath)));
    auto *pathItem = new QTableWidgetItem(QDir::toNativeSeparators(mapping.localPath));
    pathItem->setData(Qt::UserRole, mapping.localPath);
    pathItem->setData(rowKindRole, QLatin1String(manualRowKindC));
    _table->setItem(row, 1, pathItem);
    _table->setItem(row, 2, new QTableWidgetItem(tr("Personal (manual)")));

    auto *combo = new QComboBox(this);
    combo->addItem(tr("No drive"), QVariant::fromValue(QChar()));
    auto letters = DriveMappingManager::availableDriveLetters();
    if (!mapping.driveLetter.isNull() && !letters.contains(mapping.driveLetter))
        letters.append(mapping.driveLetter);
    std::sort(letters.begin(), letters.end());
    for (auto letter : std::as_const(letters))
        combo->addItem(QStringLiteral("%1:").arg(letter), QVariant::fromValue(letter));
    combo->setCurrentIndex(mapping.driveLetter.isNull() ? 0 : combo->findData(QVariant::fromValue(mapping.driveLetter)));
    connect(combo, &QComboBox::currentIndexChanged, this, [this, path = mapping.localPath, combo](int index) {
        slotManualLetterChanged(path, combo->itemData(index).toChar());
    });
    _table->setCellWidget(row, 3, combo);
}

void DriveMappingSettings::buildPolicyRow(const DriveMappingManager::PolicyMapping &mapping, int row)
{
    _table->setItem(row, 0, new QTableWidgetItem(mapping.relativePathHint.isEmpty() ? extractFolderNameFromPath(mapping.folderPath) : mapping.relativePathHint));

    const auto pathText = mapping.resolved
        ? QDir::toNativeSeparators(mapping.localPath)
        : tr("The folder is not present locally.");
    auto *pathItem = new QTableWidgetItem(pathText);
    if (!mapping.resolved) {
        pathItem->setBackground(QColor(255, 128, 128, 128));
    }
    pathItem->setData(Qt::UserRole, mapping.folderId);
    pathItem->setData(rowKindRole, QLatin1String(policyRowKindC));
    pathItem->setData(driveLetterRole, QString(mapping.driveLetter));
    pathItem->setData(enforcementRole, mapping.enforcement);
    _table->setItem(row, 1, pathItem);

    const auto enforcement = mapping.enforcement.isEmpty() ? tr("suggested") : mapping.enforcement;
    _table->setItem(row, 2, new QTableWidgetItem(tr("Company managed policy (%1)").arg(enforcement)));

    auto *combo = new QComboBox(this);
    combo->addItem(mapping.driveLetter.isNull() ? tr("No drive") : QStringLiteral("%1:").arg(mapping.driveLetter), QVariant::fromValue(mapping.driveLetter));
    combo->setCurrentIndex(0);
    combo->setEnabled(false);
    combo->setToolTip(mapping.enforcement == QLatin1String(enforcedC)
            ? tr("This drive mapping is enforced by administrator policy.")
            : tr("This drive mapping was suggested by administrator policy."));
    _table->setCellWidget(row, 3, combo);
}

void DriveMappingSettings::slotLetterChanged(Folder *folder, QChar letter)
{
    if (letter == folder->driveLetter())
        return;
    folder->setDriveLetter(letter);
    refresh();
}

void DriveMappingSettings::slotManualLetterChanged(const QString &localPath, QChar letter)
{
    FolderMan::instance()->driveMappingManager().setManualMappingDriveLetter(_accountState, localPath, letter);
    refresh();
}

void DriveMappingSettings::slotAddManualMapping()
{
    const auto dir = QFileDialog::getExistingDirectory(this,
        tr("Select folder to map"),
        QDir::homePath(),
        QFileDialog::ShowDirsOnly);
    if (dir.isEmpty())
        return;

    auto &mappingManager = FolderMan::instance()->driveMappingManager();
    if (!mappingManager.addManualMapping(_accountState, dir)) {
        QMessageBox::information(this, tr("Drive mapping"), tr("This folder is already in the manual mapping list."));
        return;
    }
    refresh();
}

void DriveMappingSettings::slotRemoveManualMapping(const QString &localPath)
{
    FolderMan::instance()->driveMappingManager().removeManualMapping(_accountState, localPath);
    refresh();
}

void DriveMappingSettings::slotShowContextMenu(const QPoint &pos)
{
    const auto index = _table->indexAt(pos);
    if (!index.isValid())
        return;

    const auto *pathItem = _table->item(index.row(), 1);
    if (!pathItem)
        return;

    const auto localPath = pathItem->data(Qt::UserRole).toString();
    const auto rowKind = pathItem->data(rowKindRole).toString();
    if (localPath.isEmpty() && rowKind != QLatin1String(policyRowKindC))
        return;

    QMenu menu(this);
    if (rowKind == QLatin1String(manualRowKindC)) {
        menu.addAction(tr("Remove"), this, [this, localPath] {
            slotRemoveManualMapping(localPath);
        });
    } else if (rowKind == QLatin1String(policyRowKindC)) {
        const auto folderId = pathItem->data(Qt::UserRole).toString();
        const auto driveLetterString = pathItem->data(driveLetterRole).toString();
        const auto driveLetter = driveLetterString.isEmpty() ? QChar() : driveLetterString.at(0);
        const auto enforcement = pathItem->data(enforcementRole).toString();
        menu.addAction(tr("Why can't I change this?"), this, [this] {
            QMessageBox::information(this, tr("Drive mapping"), tr("This drive mapping is an enforced company managed policy and it cannot be remapped or removed."));
        });
    }
    menu.exec(_table->viewport()->mapToGlobal(pos));
}

void DriveMappingSettings::slotMappingFailed(const QString &folderAlias, const QString &message)
{
    Q_UNUSED(folderAlias)
    QMessageBox::warning(this, tr("Drive mapping"), message);
    refresh();
}

void DriveMappingSettings::slotRemoveAll()
{
    const auto folders = FolderMan::instance()->map().values();
    const auto mappedCount = std::count_if(folders.cbegin(), folders.cend(), [this](Folder *folder) {
        return folder->accountState() == _accountState && !folder->driveLetter().isNull();
    });
    const auto manualMappings = FolderMan::instance()->driveMappingManager().manualMappings(_accountState);
    const auto manualMappedCount = std::count_if(manualMappings.cbegin(), manualMappings.cend(), [](const DriveMappingManager::ManualMapping &mapping) {
        return !mapping.driveLetter.isNull();
    });
    const auto totalMappedCount = mappedCount + manualMappedCount;
    if (totalMappedCount == 0)
        return;

    if (QMessageBox::question(this, tr("Remove all mappings"),
            tr("Remove all %1 drive mapping(s) for this account?").arg(totalMappedCount))
        != QMessageBox::Yes) {
        return;
    }

    for (auto *folder : folders) {
        if (folder->accountState() == _accountState && !folder->driveLetter().isNull())
            folder->setDriveLetter(QChar());
    }
    for (const auto &mapping : manualMappings) {
        if (!mapping.driveLetter.isNull())
            FolderMan::instance()->driveMappingManager().setManualMappingDriveLetter(_accountState, mapping.localPath, QChar());
    }
    refresh();
}

#endif // Q_OS_WIN

} // namespace OCC
