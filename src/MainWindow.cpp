#include "MainWindow.h"
#include "RegistryMgr.h"
#include "FileShredder.h"
#include "BrowserCleaner.h"
#include "NtfsRecovery.h"
#include "FatRecovery.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QFileDialog>
#include <QApplication>
#include <QStorageInfo>
#include <QFileIconProvider>
#include <QElapsedTimer>
#include <QThread>
#include <QMenu>
#include <QStyle>
#include <QDesktopServices>
#include <QUrl>
#include <QHeaderView>
#include <windows.h>

class SortTreeItem : public QTreeWidgetItem {
public:
    SortTreeItem(const QStringList& strings) : QTreeWidgetItem(strings) {}
    bool operator<(const QTreeWidgetItem &other) const override {
        int column = treeWidget() ? treeWidget()->sortColumn() : 0;
        bool asc = treeWidget() ? (treeWidget()->header()->sortIndicatorOrder() == Qt::AscendingOrder) : true;
        
        if (column == 0) {
            bool isUnknown1 = (text(0) == "Unknown Folders");
            bool isUnknown2 = (other.text(0) == "Unknown Folders");
            
            if (isUnknown1 && !isUnknown2) return asc ? false : true; 
            if (!isUnknown1 && isUnknown2) return asc ? true : false;  
            
            // Folders always on top
            bool isFolder1 = !data(0, Qt::UserRole).isValid();
            bool isFolder2 = !other.data(0, Qt::UserRole).isValid();
            if (isFolder1 != isFolder2) {
                return asc ? isFolder1 : !isFolder1;
            }
        }
        
        if (column == 2) { // Size column (numeric sort)
            bool ok1, ok2;
            qint64 size1 = text(2).toLongLong(&ok1);
            qint64 size2 = other.text(2).toLongLong(&ok2);
            if (ok1 && ok2) {
                return size1 < size2;
            }
        }
        
        return QTreeWidgetItem::operator<(other);
    }
};

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("ClearMax - Ultimate PC Cleaner & Shredder");
    resize(1100, 650);
    setupUi();
    loadPrograms();
}

MainWindow::~MainWindow() {}

void MainWindow::setupUi() {
    QWidget* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    
    QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);
    
    tabWidget = new QTabWidget(this);
    mainLayout->addWidget(tabWidget);
    
    progressBar = new QProgressBar(this);
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    mainLayout->addWidget(progressBar);
    
    lblStatus = new QLabel("Ready", this);
    lblStatus->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(lblStatus);
    
    // --- Tab 1: Program Management ---
    QWidget* tabPrograms = new QWidget();
    QVBoxLayout* layoutPrograms = new QVBoxLayout(tabPrograms);
    
    // Add Search Box
    QHBoxLayout* searchLayout = new QHBoxLayout();
    searchLayout->addWidget(new QLabel("Search:"));
    searchBox = new QLineEdit();
    searchBox->setPlaceholderText("Type program name to filter...");
    searchLayout->addWidget(searchBox);
    layoutPrograms->addLayout(searchLayout);

    programsTable = new QTableWidget();
    programsTable->setColumnCount(6);
    programsTable->setHorizontalHeaderLabels({"Name", "Publisher", "Version", "Size", "Install Date", "Ghost?"});
    programsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive); // Allow user to resize columns
    programsTable->horizontalHeader()->setStretchLastSection(true); // Stretch the last column to fill remaining space
    programsTable->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    programsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    programsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    programsTable->setSortingEnabled(true); // Enable sorting when header is clicked
    programsTable->setContextMenuPolicy(Qt::CustomContextMenu);
    layoutPrograms->addWidget(programsTable);
    
    QHBoxLayout* layoutProgramBtns = new QHBoxLayout();
    btnRefreshPrograms = new QPushButton("Refresh List");
    btnUninstall = new QPushButton("Normal Uninstall");
    btnForceRemove = new QPushButton("Force Remove (Ghost)");
    
    layoutProgramBtns->addWidget(btnRefreshPrograms);
    layoutProgramBtns->addStretch();
    layoutProgramBtns->addWidget(btnUninstall);
    layoutProgramBtns->addWidget(btnForceRemove);
    layoutPrograms->addLayout(layoutProgramBtns);
    
    tabWidget->addTab(tabPrograms, "Program Management");
    
    connect(btnRefreshPrograms, &QPushButton::clicked, this, &MainWindow::loadPrograms);
    connect(btnUninstall, &QPushButton::clicked, this, &MainWindow::uninstallSelected);
    connect(btnForceRemove, &QPushButton::clicked, this, &MainWindow::forceRemoveSelected);
    connect(searchBox, &QLineEdit::textChanged, this, &MainWindow::filterPrograms);
    connect(programsTable, &QTableWidget::itemSelectionChanged, this, [this]() {
        bool hasSelection = programsTable->selectedItems().count() > 0;
        btnUninstall->setEnabled(hasSelection);
        btnForceRemove->setEnabled(hasSelection);
    });
    connect(programsTable, &QTableWidget::customContextMenuRequested, this, &MainWindow::showProgramsContextMenu);

    // --- Tab 2: File Shredder ---
    QWidget* tabShredder = new QWidget();
    QVBoxLayout* layoutShredder = new QVBoxLayout(tabShredder);
    
    layoutShredder->addWidget(new QLabel("<b>Secure File Deletion</b>"));
    
    QHBoxLayout* fileLayout = new QHBoxLayout();
    lblSelectedFile = new QLabel("No file/folder selected.");
    btnBrowseFile = new QPushButton("Browse File...");
    btnBrowseFolder = new QPushButton("Browse Folder...");
    fileLayout->addWidget(lblSelectedFile, 1);
    fileLayout->addWidget(btnBrowseFile);
    fileLayout->addWidget(btnBrowseFolder);
    layoutShredder->addLayout(fileLayout);
    
    QHBoxLayout* passLayout = new QHBoxLayout();
    passLayout->addWidget(new QLabel("Security Level:"));
    comboPasses = new QComboBox();
    comboPasses->addItem("0-Pass (Fast Delete/SSD)", QVariant(static_cast<int>(ShredPass::Pass_0)));
    comboPasses->addItem("1-Pass (Quick)", QVariant(static_cast<int>(ShredPass::Pass_1)));
    comboPasses->addItem("3-Pass (DoD 5220.22-M)", QVariant(static_cast<int>(ShredPass::Pass_3)));
    comboPasses->addItem("7-Pass (Secure)", QVariant(static_cast<int>(ShredPass::Pass_7)));
    passLayout->addWidget(comboPasses);
    passLayout->addStretch();
    layoutShredder->addLayout(passLayout);
    
    QHBoxLayout* shredBtnLayout = new QHBoxLayout();
    btnShred = new QPushButton("Shred File Permanently");
    btnShred->setStyleSheet("QPushButton { background-color: #ffcccc; color: red; font-weight: bold; }");
    shredBtnLayout->addWidget(btnShred);
    
    btnPauseShred = new QPushButton("Pause");
    btnCancelShred = new QPushButton("Stop");
    btnPauseShred->setVisible(false);
    btnCancelShred->setVisible(false);
    shredBtnLayout->addWidget(btnPauseShred);
    shredBtnLayout->addWidget(btnCancelShred);
    layoutShredder->addLayout(shredBtnLayout);
    
    layoutShredder->addSpacing(20);
    layoutShredder->addWidget(new QLabel("<b>Free Space Wiping</b> (Obfuscate already deleted files)"));
    
    QHBoxLayout* wipeLayout = new QHBoxLayout();
    wipeLayout->addWidget(new QLabel("Select Drive:"));
    comboDrives = new QComboBox();
    comboDrives->addItem("All Drives");
    for (const QStorageInfo &storage : QStorageInfo::mountedVolumes()) {
        if (storage.isValid() && storage.isReady() && !storage.isReadOnly()) {
            comboDrives->addItem(storage.rootPath());
        }
    }
    wipeLayout->addWidget(comboDrives);
    wipeLayout->addStretch();
    layoutShredder->addLayout(wipeLayout);
    
    QHBoxLayout* wipeModeLayout = new QHBoxLayout();
    wipeModeLayout->addWidget(new QLabel("Wipe Mode:"));
    comboWipeMode = new QComboBox();
    comboWipeMode->addItem("MFT (Table) Only - SSD Recommended", QVariant(static_cast<int>(WipeMode::MftOnly)));
    comboWipeMode->addItem("Full Free Space & MFT Wipe", QVariant(static_cast<int>(WipeMode::FullWipe)));
    wipeModeLayout->addWidget(comboWipeMode);
    wipeModeLayout->addStretch();
    layoutShredder->addLayout(wipeModeLayout);
    
    btnWipeFreeSpace = new QPushButton("Wipe Free Space");
    layoutShredder->addWidget(btnWipeFreeSpace);
    
    QHBoxLayout* wipeControlsLayout = new QHBoxLayout();
    btnPauseWipe = new QPushButton("Pause");
    btnCancelWipe = new QPushButton("Stop");
    btnPauseWipe->setVisible(false);
    btnCancelWipe->setVisible(false);
    wipeControlsLayout->addWidget(btnPauseWipe);
    wipeControlsLayout->addWidget(btnCancelWipe);
    layoutShredder->addLayout(wipeControlsLayout);
    
    layoutShredder->addStretch();
    tabWidget->addTab(tabShredder, "File Shredder");
    
    connect(btnBrowseFile, &QPushButton::clicked, this, &MainWindow::browseFileToShred);
    connect(btnBrowseFolder, &QPushButton::clicked, this, &MainWindow::browseFolderToShred);
    connect(btnShred, &QPushButton::clicked, this, &MainWindow::shredSelectedFile);
    connect(btnPauseShred, &QPushButton::clicked, this, &MainWindow::pauseOperation);
    connect(btnCancelShred, &QPushButton::clicked, this, &MainWindow::cancelOperation);
    connect(btnWipeFreeSpace, &QPushButton::clicked, this, &MainWindow::wipeFreeSpace);
    connect(btnPauseWipe, &QPushButton::clicked, this, &MainWindow::pauseOperation);
    connect(btnCancelWipe, &QPushButton::clicked, this, &MainWindow::cancelOperation);

    // --- Tab 3: Browser Cleaner ---
    QWidget* tabBrowser = new QWidget();
    QVBoxLayout* layoutBrowser = new QVBoxLayout(tabBrowser);
    
    layoutBrowser->addWidget(new QLabel("<b>Select Browsers to Clean</b>"));
    chkChrome = new QCheckBox("Google Chrome");
    chkEdge = new QCheckBox("Microsoft Edge");
    chkFirefox = new QCheckBox("Mozilla Firefox");
    layoutBrowser->addWidget(chkChrome);
    layoutBrowser->addWidget(chkEdge);
    layoutBrowser->addWidget(chkFirefox);
    
    layoutBrowser->addSpacing(10);
    layoutBrowser->addWidget(new QLabel("<b>Select Data to Shred</b>"));
    chkHistory = new QCheckBox("History");
    chkCookies = new QCheckBox("Cookies");
    chkCache = new QCheckBox("Cache");
    layoutBrowser->addWidget(chkHistory);
    layoutBrowser->addWidget(chkCookies);
    layoutBrowser->addWidget(chkCache);
    
    btnCleanBrowsers = new QPushButton("Securely Clean Browsers");
    btnCleanBrowsers->setStyleSheet("QPushButton { background-color: #ffcccc; color: red; font-weight: bold; }");
    layoutBrowser->addSpacing(20);
    layoutBrowser->addWidget(btnCleanBrowsers);
    layoutBrowser->addStretch();
    
    tabWidget->addTab(tabBrowser, "Browser Cleaner");
    
    connect(btnCleanBrowsers, &QPushButton::clicked, this, &MainWindow::cleanBrowserData);

    // --- Tab 4: File Recovery ---
    QWidget* tabRecovery = new QWidget();
    QVBoxLayout* layoutRecovery = new QVBoxLayout(tabRecovery);
    
    QHBoxLayout* recoveryTopLayout = new QHBoxLayout();
    recoveryTopLayout->addWidget(new QLabel("Select Drive:"));
    comboRecoveryDrives = new QComboBox();
    for (const QStorageInfo &storage : QStorageInfo::mountedVolumes()) {
        if (storage.isValid() && storage.isReady() && !storage.isReadOnly()) {
            QString fs = storage.fileSystemType();
            if (fs.compare("NTFS", Qt::CaseInsensitive) == 0 || fs.compare("FAT", Qt::CaseInsensitive) == 0 || fs.compare("FAT32", Qt::CaseInsensitive) == 0 || fs.compare("exFAT", Qt::CaseInsensitive) == 0) {
                comboRecoveryDrives->addItem(storage.rootPath());
            }
        }
    }
    recoveryTopLayout->addWidget(comboRecoveryDrives);
    btnScanDrive = new QPushButton("Scan for Deleted Files");
    recoveryTopLayout->addWidget(btnScanDrive);
    recoveryTopLayout->addStretch();
    layoutRecovery->addLayout(recoveryTopLayout);
    
    QHBoxLayout* searchRecLayout = new QHBoxLayout();
    searchRecLayout->addWidget(new QLabel("Search:"));
    searchRecoveryBox = new QLineEdit();
    searchRecoveryBox->setPlaceholderText("Type filename or extension to filter...");
    searchRecLayout->addWidget(searchRecoveryBox);
    layoutRecovery->addLayout(searchRecLayout);
    
    recoveryTree = new QTreeWidget();
    recoveryTree->setColumnCount(4);
    recoveryTree->setHeaderLabels({"Name", "Extension", "Size (Bytes)", "Recoverability"});
    recoveryTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    recoveryTree->setSelectionBehavior(QAbstractItemView::SelectRows);
    recoveryTree->setSelectionMode(QAbstractItemView::SingleSelection);
    recoveryTree->setSortingEnabled(true);
    recoveryTree->setContextMenuPolicy(Qt::CustomContextMenu);
    layoutRecovery->addWidget(recoveryTree);
    
    btnRecoverSelected = new QPushButton("Recover Selected File");
    btnRecoverSelected->setEnabled(false);
    layoutRecovery->addWidget(btnRecoverSelected);
    
    tabWidget->addTab(tabRecovery, "File Recovery");
    
    connect(btnScanDrive, &QPushButton::clicked, this, &MainWindow::scanRecoveryDrive);
    connect(btnRecoverSelected, &QPushButton::clicked, this, &MainWindow::recoverSelectedFile);
    connect(searchRecoveryBox, &QLineEdit::textChanged, this, &MainWindow::filterRecoveryFiles);
    connect(recoveryTree, &QTreeWidget::itemSelectionChanged, this, [this]() {
        btnRecoverSelected->setEnabled(recoveryTree->selectedItems().count() > 0);
    });
    connect(recoveryTree, &QTreeWidget::customContextMenuRequested, this, &MainWindow::showRecoveryContextMenu);
}

void MainWindow::loadPrograms() {
    programsTable->setSortingEnabled(false); // Disable sorting while populating
    programsTable->setRowCount(0);
    QList<ProgramInfo> programs = RegistryMgr::getInstalledPrograms();
    
    QFileIconProvider iconProvider;
    
    for (int i = 0; i < programs.size(); ++i) {
        programsTable->insertRow(i);
        
        QTableWidgetItem* nameItem = new QTableWidgetItem(programs[i].displayName);
        // Try to get icon
        QIcon icon;
        QString iconPath = programs[i].displayIcon;
        if (!iconPath.isEmpty()) {
            if (iconPath.contains(",")) {
                iconPath = iconPath.split(",").first(); // Extract path before comma
            }
            iconPath.remove("\"");
            QFileInfo fi(iconPath);
            if (fi.exists()) {
                icon = iconProvider.icon(fi);
            }
        }
        if (icon.isNull() && !programs[i].uninstallString.isEmpty()) {
            QString exePath = programs[i].uninstallString;
            if (exePath.contains("\"")) {
                exePath = exePath.split("\"")[1];
            }
            QFileInfo fi(exePath);
            if (fi.exists() && fi.suffix().toLower() == "exe") {
                icon = iconProvider.icon(fi);
            }
        }
        if (!icon.isNull()) {
            nameItem->setIcon(icon);
        }
        
        programsTable->setItem(i, 0, nameItem);
        programsTable->setItem(i, 1, new QTableWidgetItem(programs[i].publisher));
        programsTable->setItem(i, 2, new QTableWidgetItem(programs[i].displayVersion));
        
        QString sizeStr = "";
        if (programs[i].estimatedSize > 0) {
            double mb = programs[i].estimatedSize / 1024.0;
            if (mb > 1024) {
                sizeStr = QString::number(mb / 1024.0, 'f', 2) + " GB";
            } else {
                sizeStr = QString::number(mb, 'f', 2) + " MB";
            }
        }
        
        QTableWidgetItem* sizeItem = new QTableWidgetItem(sizeStr);
        // Set data for proper numerical sorting instead of string sorting
        sizeItem->setData(Qt::UserRole + 1, static_cast<qulonglong>(programs[i].estimatedSize));
        programsTable->setItem(i, 3, sizeItem);
        
        QString dateStr = programs[i].installDate;
        if (dateStr.length() == 8) { // format YYYY-MM-DD
            dateStr = dateStr.mid(0, 4) + "-" + dateStr.mid(4, 2) + "-" + dateStr.mid(6, 2);
        }
        programsTable->setItem(i, 4, new QTableWidgetItem(dateStr));
        
        programsTable->setItem(i, 5, new QTableWidgetItem(programs[i].isGhost ? "Yes" : "No"));
        
        // Store the full info in the first item's user data
        QVariant var;
        var.setValue(programs[i].uninstallString + "|" + programs[i].registryKeyPath);
        programsTable->item(i, 0)->setData(Qt::UserRole, var);
    }
    programsTable->setSortingEnabled(true);
    programsTable->resizeColumnsToContents(); // Initially size to fit, but user can still resize because of Interactive mode
}

void MainWindow::filterPrograms(const QString& text) {
    for (int i = 0; i < programsTable->rowCount(); ++i) {
        bool match = false;
        QTableWidgetItem* item = programsTable->item(i, 0); // Name column
        if (item && item->text().contains(text, Qt::CaseInsensitive)) {
            match = true;
        }
        programsTable->setRowHidden(i, !match);
    }
}

void MainWindow::uninstallSelected() {
    int row = programsTable->currentRow();
    if (row < 0) return;
    
    QString data = programsTable->item(row, 0)->data(Qt::UserRole).toString();
    QString uninstallString = data.split("|")[0];
    
    if (uninstallString.isEmpty()) {
        QMessageBox::warning(this, "Error", "No uninstall string found for this program.");
        return;
    }
    
    ProgramInfo dummy;
    dummy.uninstallString = uninstallString;
    RegistryMgr::uninstallProgram(dummy, [this]() {
        QMetaObject::invokeMethod(this, [this]() {
            loadPrograms();
        }, Qt::QueuedConnection);
    });
}

void MainWindow::forceRemoveSelected() {
    int row = programsTable->currentRow();
    if (row < 0) return;
    
    QString name = programsTable->item(row, 0)->text();
    QString data = programsTable->item(row, 0)->data(Qt::UserRole).toString();
    QString regKey = data.split("|")[1];
    
    QMessageBox::StandardButton reply = QMessageBox::warning(this, "Warning", 
        "Are you sure you want to forcefully remove the registry keys for '" + name + "'? This action cannot be undone.",
        QMessageBox::Yes | QMessageBox::No);
        
    if (reply == QMessageBox::Yes) {
        ProgramInfo dummy;
        dummy.registryKeyPath = regKey;
        RegistryMgr::forceRemoveProgram(dummy);
        loadPrograms();
    }
}

void MainWindow::browseFileToShred() {
    QString file = QFileDialog::getOpenFileName(this, "Select File to Shred");
    if (!file.isEmpty()) {
        currentFileToShred = file;
        lblSelectedFile->setText(file);
    }
}

void MainWindow::browseFolderToShred() {
    QString folder = QFileDialog::getExistingDirectory(this, "Select Folder to Shred");
    if (!folder.isEmpty()) {
        currentFileToShred = folder;
        lblSelectedFile->setText(folder);
    }
}

void MainWindow::shredSelectedFile() {
    if (currentFileToShred.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please select a file or folder first.");
        return;
    }
    
    QMessageBox::StandardButton reply = QMessageBox::critical(this, "CRITICAL WARNING", 
        "You are about to PERMANENTLY DESTROY the selected path.\nIt will NOT be recoverable by ANY means.\n\nAre you absolutely sure?",
        QMessageBox::Yes | QMessageBox::No);
        
    if (reply == QMessageBox::Yes) {
        btnShred->setEnabled(false);
        btnPauseShred->setVisible(true);
        btnCancelShred->setVisible(true);
        btnPauseShred->setText("Pause");
        btnPauseShred->setEnabled(true);
        m_shredState = ShredState::Running;
        
        progressBar->setValue(0);
        
        ShredPass passes = static_cast<ShredPass>(comboPasses->currentData().toInt());
        
        if (passes != ShredPass::Pass_1 && FileShredder::isDriveSSD(currentFileToShred)) {
            QMessageBox::information(this, "SSD Detected", "The selected path is on an SSD.\nTo protect your SSD's lifespan and because wear-leveling makes multiple passes ineffective, the security level has been automatically adjusted to 1-Pass (Quick).");
            passes = ShredPass::Pass_1;
            // Optionally update UI combo box to reflect this
            comboPasses->setCurrentIndex(0);
        }
        
        QElapsedTimer timer;
        timer.start();
        
        auto cancelCheck = [this]() -> bool {
            QApplication::processEvents();
            while (m_shredState == ShredState::Paused) {
                QThread::msleep(50);
                QApplication::processEvents();
            }
            return m_shredState == ShredState::Cancelled;
        };
        
        int result = FileShredder::shredPath(currentFileToShred, passes, [this, &timer](int progress, const QString& statusMsg) {
            this->progressBar->setValue(progress);
            if (!statusMsg.isEmpty()) {
                if (statusMsg.startsWith("Gathering files")) {
                    this->lblStatus->setText(statusMsg);
                    timer.restart(); // Restart timer so ETA doesn't include gathering time
                } else if (progress > 0 && progress < 100) {
                    qint64 elapsedMs = timer.elapsed();
                    qint64 totalEstimatedMs = (elapsedMs * 100) / progress;
                    qint64 remainingMs = totalEstimatedMs - elapsedMs;
                    int remainingSec = static_cast<int>(remainingMs / 1000);
                    int min = remainingSec / 60;
                    int sec = remainingSec % 60;
                    
                    QString displayMsg = statusMsg;
                    if (displayMsg.length() > 60) {
                        displayMsg = "..." + displayMsg.right(57);
                    }
                    
                    this->lblStatus->setText(QString("ETA: %1m %2s | %3")
                        .arg(min).arg(sec, 2, 10, QChar('0')).arg(displayMsg));
                } else {
                    this->lblStatus->setText(statusMsg);
                }
            }
            QApplication::processEvents(); // Keep UI responsive
        }, cancelCheck);
        
        progressBar->setValue(100);
        btnShred->setEnabled(true);
        btnPauseShred->setVisible(false);
        btnCancelShred->setVisible(false);
        
        if (m_shredState == ShredState::Cancelled) {
            lblStatus->setText("Cancelled");
            QMessageBox::information(this, "Cancelled", "Shredding process was stopped by user.");
            lblStatus->setText("Ready");
        } else if (result == 0) {
            QMessageBox::information(this, "Success", "Selected path has been securely shredded.");
            currentFileToShred.clear();
            lblSelectedFile->setText("No file/folder selected.");
            lblStatus->setText("Ready");
        } else if (result > 0) {
            QMessageBox::warning(this, "Partial Success", QString("Shredding completed, but %1 file(s) could not be deleted.\n(They might be in use by another program or protected by the system)").arg(result));
            lblStatus->setText("Ready");
        } else {
            QMessageBox::warning(this, "Error", "Failed to access the selected path or invalid path.");
            lblStatus->setText("Ready");
        }
    }
}

void MainWindow::wipeFreeSpace() {
    QString selectedDrive = comboDrives->currentText();
    if (selectedDrive.isEmpty()) return;
    
    QStringList drivesToWipe;
    if (selectedDrive == "All Drives") {
        for (const QStorageInfo &storage : QStorageInfo::mountedVolumes()) {
            if (storage.isValid() && storage.isReady() && !storage.isReadOnly()) {
                drivesToWipe.append(storage.rootPath());
            }
        }
    } else {
        drivesToWipe.append(selectedDrive);
    }
    
    if (drivesToWipe.isEmpty()) return;
    
    QMessageBox::StandardButton reply = QMessageBox::warning(this, "Warning", 
        "Wiping free space on " + selectedDrive + " can take a significant amount of time and will cause high disk usage.\nContinue?",
        QMessageBox::Yes | QMessageBox::No);
        
    if (reply == QMessageBox::Yes) {
        btnWipeFreeSpace->setEnabled(false);
        btnPauseWipe->setVisible(true);
        btnCancelWipe->setVisible(true);
        btnPauseWipe->setText("Pause");
        btnPauseWipe->setEnabled(true);
        m_shredState = ShredState::Running;
        
        bool allSuccess = true;
        
        for (const QString& currentDrive : drivesToWipe) {
            if (m_shredState == ShredState::Cancelled) break;
            
            progressBar->setValue(0);
            lblStatus->setText("Starting wipe process on " + currentDrive + "...");
            
            QElapsedTimer timer;
            timer.start();
            
            auto progressCallback = [this, &timer, currentDrive](int progress) {
                this->progressBar->setValue(progress);
                if (progress > 0 && progress < 100) {
                    if (m_shredState == ShredState::Paused) {
                        this->lblStatus->setText("Wiping Paused (" + currentDrive + ")");
                    } else {
                        qint64 elapsedMs = timer.elapsed();
                        qint64 totalEstimatedMs = (elapsedMs * 100) / progress;
                        qint64 remainingMs = totalEstimatedMs - elapsedMs;
                        int remainingSec = remainingMs / 1000;
                        int min = remainingSec / 60;
                        int sec = remainingSec % 60;
                        this->lblStatus->setText(QString("Wiping Free Space on %1... %2% (ETA: %3m %4s)").arg(currentDrive).arg(progress).arg(min).arg(sec, 2, 10, QChar('0')));
                    }
                } else if (progress == 100) {
                    this->lblStatus->setText("Finalizing " + currentDrive + "... (Flushing data to disk, please wait)");
                }
                QApplication::processEvents();
            };
            
            auto cancelCheck = [this]() -> bool {
                QApplication::processEvents();
                while (m_shredState == ShredState::Paused) {
                    // Yield to event loop while paused
                    QThread::msleep(50);
                    QApplication::processEvents();
                }
                return m_shredState == ShredState::Cancelled;
            };
            
            WipeMode mode = static_cast<WipeMode>(comboWipeMode->currentData().toInt());
            bool success = FileShredder::wipeFreeSpace(currentDrive, mode, progressCallback, cancelCheck);
            if (!success) {
                allSuccess = false;
            }
        }
        
        btnWipeFreeSpace->setEnabled(true);
        btnPauseWipe->setVisible(false);
        btnCancelWipe->setVisible(false);
        
        if (m_shredState == ShredState::Cancelled) {
            lblStatus->setText("Wipe Cancelled");
            QMessageBox::information(this, "Cancelled", "Wiping process was stopped by user.");
        } else if (allSuccess) {
            lblStatus->setText("Ready");
            QMessageBox::information(this, "Success", "Free space wiped successfully.");
        } else {
            lblStatus->setText("Error");
            QMessageBox::warning(this, "Error", "Failed to complete free space wipe on some drives.");
        }
    }
}

void MainWindow::pauseOperation() {
    if (m_shredState == ShredState::Running) {
        m_shredState = ShredState::Paused;
        btnPauseShred->setText("Resume");
        btnPauseWipe->setText("Resume");
    } else if (m_shredState == ShredState::Paused) {
        m_shredState = ShredState::Running;
        btnPauseShred->setText("Pause");
        btnPauseWipe->setText("Pause");
    }
}

void MainWindow::cancelOperation() {
    m_shredState = ShredState::Cancelled;
    btnPauseShred->setEnabled(false);
    btnPauseWipe->setEnabled(false);
}

void MainWindow::cleanBrowserData() {
    QList<BrowserDataType> types;
    if (chkHistory->isChecked()) types.append(BrowserDataType::History);
    if (chkCookies->isChecked()) types.append(BrowserDataType::Cookies);
    if (chkCache->isChecked()) types.append(BrowserDataType::Cache);
    
    if (types.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please select at least one data type to shred.");
        return;
    }
    
    QMessageBox::StandardButton reply = QMessageBox::critical(this, "Warning", 
        "Selected browser data will be securely shredded and cannot be recovered.\nEnsure browsers are closed before proceeding.\n\nContinue?",
        QMessageBox::Yes | QMessageBox::No);
        
    if (reply == QMessageBox::Yes) {
        btnCleanBrowsers->setEnabled(false);
        progressBar->setValue(0);
        ShredPass passes = static_cast<ShredPass>(comboPasses->currentData().toInt()); // Use the pass combo from Shredder tab
        
        if (chkChrome->isChecked()) BrowserCleaner::cleanBrowserData(BrowserType::Chrome, types, passes, [this](int p){ progressBar->setValue(p); QApplication::processEvents(); });
        if (chkEdge->isChecked()) BrowserCleaner::cleanBrowserData(BrowserType::Edge, types, passes, [this](int p){ progressBar->setValue(p); QApplication::processEvents(); });
        if (chkFirefox->isChecked()) BrowserCleaner::cleanBrowserData(BrowserType::Firefox, types, passes, [this](int p){ progressBar->setValue(p); QApplication::processEvents(); });
        
        progressBar->setValue(100);
        btnCleanBrowsers->setEnabled(true);
        QMessageBox::information(this, "Success", "Browser data securely shredded.");
    }
}

void MainWindow::scanRecoveryDrive() {
    if (comboRecoveryDrives->currentIndex() == -1) return;
    
    btnScanDrive->setEnabled(false);
    btnRecoverSelected->setEnabled(false);
    recoveryTree->clear();
    m_recoverableFiles.clear();
        this->lblStatus->setText("Detecting file system...");
    this->progressBar->setValue(0);
    QApplication::processEvents();

    QString drivePath = comboRecoveryDrives->currentText();
    QString driveRoot = drivePath.left(3);
    
    wchar_t fsNameBuf[MAX_PATH];
    GetVolumeInformationW(reinterpret_cast<const wchar_t*>(driveRoot.utf16()), NULL, 0, NULL, NULL, NULL, fsNameBuf, MAX_PATH);
    QString fsName = QString::fromWCharArray(fsNameBuf);
    
    bool isFat = fsName.contains("FAT", Qt::CaseInsensitive); // Covers FAT, FAT32, exFAT
    
    QThread* thread = QThread::create([this, drivePath, isFat]() {
        bool success = false;
        
        auto progressCallback = [this](int progress, const QString& statusMsg) {
            QMetaObject::invokeMethod(this, [this, progress, statusMsg]() {
                this->progressBar->setValue(progress);
                this->lblStatus->setText(statusMsg);
                QApplication::processEvents();
            }, Qt::QueuedConnection);
        };
        
        if (isFat) {
            FatRecovery fat;
            success = fat.scanDrive(drivePath, m_recoverableFiles, progressCallback);
        } else {
            NtfsRecovery ntfs;
            success = ntfs.scanDrive(drivePath, m_recoverableFiles, progressCallback);
        }
        
        QMetaObject::invokeMethod(this, [this, success]() {
            if (success) {
                populateRecoveryTree();
                QMessageBox::information(this, "Scan Complete", QString("Found %1 deleted files.").arg(m_recoverableFiles.size()));
                this->lblStatus->setText("Scan complete.");
            } else {
                QMessageBox::critical(this, "Scan Failed", "Failed to scan the drive. Ensure the program is running as Administrator.");
                this->lblStatus->setText("Scan failed.");
                this->progressBar->setValue(0);
            }
            this->btnScanDrive->setEnabled(true);
        }, Qt::QueuedConnection);
    });
    
    thread->start();
}

void MainWindow::populateRecoveryTree() {
    recoveryTree->setSortingEnabled(false);
    recoveryTree->clear();
    
    QHash<QString, QTreeWidgetItem*> folderNodes;
    
    for (int i = 0; i < m_recoverableFiles.size(); ++i) {
        const RecoverableFile& rf = m_recoverableFiles[i];
        
        // Construct tree hierarchy
        QStringList parts = rf.fullPath.split('/', Qt::SkipEmptyParts);
        QTreeWidgetItem* parentItem = nullptr;
        QString currentPath = "";
        
        for (const QString& part : parts) {
            currentPath += "/" + part;
            if (!folderNodes.contains(currentPath)) {
                SortTreeItem* node = new SortTreeItem(QStringList() << part << "" << "" << "");
                node->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_DirIcon));
                if (parentItem) {
                    parentItem->addChild(node);
                } else {
                    recoveryTree->addTopLevelItem(node);
                }
                folderNodes.insert(currentPath, node);
                parentItem = node;
            } else {
                parentItem = folderNodes.value(currentPath);
            }
        }
        
        SortTreeItem* fileItem = new SortTreeItem(QStringList() << rf.name << rf.extension << QString::number(rf.size) << rf.recoverability);
        fileItem->setData(0, Qt::UserRole, i); // Store index
        
        if (rf.recoverability == "High") fileItem->setForeground(3, QBrush(Qt::darkGreen));
        else if (rf.recoverability == "Low") fileItem->setForeground(3, QBrush(Qt::darkYellow));
        else fileItem->setForeground(3, QBrush(Qt::red));
        
        if (parentItem) {
            parentItem->addChild(fileItem);
        } else {
            recoveryTree->addTopLevelItem(fileItem);
        }
    }
    
    recoveryTree->setSortingEnabled(true);
}

bool MainWindow::filterTreeItem(QTreeWidgetItem* item, const QString& filter) {
    bool isFolder = !item->data(0, Qt::UserRole).isValid();
    bool match = false;
    
    if (isFolder) {
        // A folder is visible if ANY of its children match
        for (int i = 0; i < item->childCount(); ++i) {
            if (filterTreeItem(item->child(i), filter)) {
                match = true;
            }
        }
        if (match && !filter.isEmpty()) {
            item->setExpanded(true); // Auto-expand if a child matched the filter
        } else if (filter.isEmpty()) {
            item->setExpanded(false); // Collapse if filter cleared
        }
    } else {
        // A file is visible if its name or extension matches the filter
        if (filter.isEmpty()) {
            match = true;
        } else {
            QString name = item->text(0).toLower();
            QString ext = item->text(1).toLower();
            match = name.contains(filter) || ext.contains(filter);
        }
    }
    
    item->setHidden(!match);
    return match;
}

void MainWindow::filterRecoveryFiles(const QString& text) {
    QString filter = text.toLower();
    
    for (int i = 0; i < recoveryTree->topLevelItemCount(); ++i) {
        filterTreeItem(recoveryTree->topLevelItem(i), filter);
    }
}

void MainWindow::recoverSelectedFile() {
    QList<QTreeWidgetItem*> selected = recoveryTree->selectedItems();
    if (selected.isEmpty()) return;
    
    QTreeWidgetItem* item = selected.first();
    QVariant data = item->data(0, Qt::UserRole);
    if (!data.isValid()) return; // Probably clicked a folder node
    
    int originalIndex = data.toInt();
    if (originalIndex < 0 || originalIndex >= m_recoverableFiles.size()) return;
    
    const RecoverableFile& fileToRecover = m_recoverableFiles[originalIndex];
    
    QString savePath = QFileDialog::getSaveFileName(this, "Save Recovered File", fileToRecover.name);
    if (savePath.isEmpty()) return;
    
    QString drivePath = comboRecoveryDrives->currentText();
    QString driveRoot = drivePath.left(3);
    char fsNameBuf[MAX_PATH];
    GetVolumeInformationA(driveRoot.toStdString().c_str(), NULL, 0, NULL, NULL, NULL, fsNameBuf, MAX_PATH);
    QString fsName = QString::fromLocal8Bit(fsNameBuf);
    
    bool isFat = fsName.contains("FAT");
    bool success = false;
    
    if (isFat) {
        FatRecovery fat;
        success = fat.recoverFile(drivePath, fileToRecover, savePath);
    } else {
        NtfsRecovery ntfs;
        success = ntfs.recoverFile(drivePath, fileToRecover, savePath);
    }
    
    if (success) {
    QMessageBox::information(this, "Success", "File successfully recovered to:\n" + savePath);
    } else {
        QMessageBox::critical(this, "Error", "Failed to recover the file. The data may have been partially or completely overwritten.");
    }
}

void MainWindow::showRecoveryContextMenu(const QPoint& pos) {
    QTreeWidgetItem* item = recoveryTree->itemAt(pos);
    if (!item) return;
    
    // Only show context menu if it's a file, not a folder
    if (!item->data(0, Qt::UserRole).isValid()) return;
    
    // Ensure the clicked item is selected
    item->setSelected(true);
    
    QMenu menu(this);
    QAction* recoverAction = menu.addAction(QApplication::style()->standardIcon(QStyle::SP_DriveFDIcon), "Recover Selected File");
    connect(recoverAction, &QAction::triggered, this, &MainWindow::recoverSelectedFile);
    
    menu.exec(recoveryTree->viewport()->mapToGlobal(pos));
}

void MainWindow::showProgramsContextMenu(const QPoint& pos) {
    QTableWidgetItem* item = programsTable->itemAt(pos);
    if (!item) return;
    
    int row = item->row();
    programsTable->selectRow(row);
    
    QMenu menu(this);
    QAction* uninstallAction = menu.addAction(QApplication::style()->standardIcon(QStyle::SP_DialogCancelButton), "Uninstall");
    QAction* forceRemoveAction = menu.addAction(QApplication::style()->standardIcon(QStyle::SP_TrashIcon), "Force Remove Registry Entry");
    
    connect(uninstallAction, &QAction::triggered, this, &MainWindow::uninstallSelected);
    connect(forceRemoveAction, &QAction::triggered, this, &MainWindow::forceRemoveSelected);
    
    menu.exec(programsTable->viewport()->mapToGlobal(pos));
}
