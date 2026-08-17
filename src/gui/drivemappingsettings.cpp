/*
 * SPDX-FileCopyrightText: 2026 Nextcloud GmbH and Nextcloud contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "drivemappingsettings.h"

#include "accountstate.h"
#include "folder.h"
#include "folderman.h"

#include <QLabel>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include "drivemappingmanager.h"

#include <QComboBox>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>

#include <algorithm>
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
    auto *info = new QLabel(tr("Map a synced folder to a drive letter so it appears like a local disk in File Explorer. "
                                "Choose a drive letter for a folder, or \u201cNo drive\u201d to remove its mapping."), this);
    info->setWordWrap(true);
    layout->addWidget(info);

    auto *toolbarLayout = new QHBoxLayout;
    auto *refreshButton = new QPushButton(tr("Refresh"), this);
    connect(refreshButton, &QPushButton::clicked, this, &DriveMappingSettings::refresh);
    toolbarLayout->addWidget(refreshButton);

    _removeAllButton = new QPushButton(tr("Remove all mappings"), this);
    connect(_removeAllButton, &QPushButton::clicked, this, &DriveMappingSettings::slotRemoveAll);
    toolbarLayout->addWidget(_removeAllButton);
    toolbarLayout->addStretch();
    layout->addLayout(toolbarLayout);

    _table = new QTableWidget(this);
    _table->setColumnCount(3);
    _table->setHorizontalHeaderLabels({ tr("Folder"), tr("Local path"), tr("Drive") });
    _table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    _table->verticalHeader()->setVisible(false);
    _table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _table->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(_table);

    connect(FolderMan::instance(), &FolderMan::folderListChanged, this, &DriveMappingSettings::refresh);
    connect(&FolderMan::instance()->driveMappingManager(), &DriveMappingManager::mappingFailed,
        this, &DriveMappingSettings::slotMappingFailed);

    refresh();
#endif
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
    _removeAllButton->setEnabled(anyMapped);
#endif
}

#ifdef Q_OS_WIN

void DriveMappingSettings::buildRow(Folder *folder, int row)
{
    _table->setItem(row, 0, new QTableWidgetItem(folder->shortGuiRemotePathOrAppName()));
    _table->setItem(row, 1, new QTableWidgetItem(QDir::toNativeSeparators(folder->path())));

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
    _table->setCellWidget(row, 2, combo);
}

void DriveMappingSettings::slotLetterChanged(Folder *folder, QChar letter)
{
    if (letter == folder->driveLetter())
        return;
    folder->setDriveLetter(letter);
    refresh();
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
    if (mappedCount == 0)
        return;

    if (QMessageBox::question(this, tr("Remove all mappings"),
            tr("Remove all %1 drive mapping(s) for this account?").arg(mappedCount))
        != QMessageBox::Yes) {
        return;
    }

    for (auto *folder : folders) {
        if (folder->accountState() == _accountState && !folder->driveLetter().isNull())
            folder->setDriveLetter(QChar());
    }
    refresh();
}

#endif // Q_OS_WIN

} // namespace OCC
