#pragma once
#include <QMainWindow>
#include <QTabWidget>
#include <QTableWidget>
#include <QProgressBar>
#include <QTreeWidget>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include "NtfsRecovery.h"

enum class ShredState {
    Running,
    Paused,
    Cancelled
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    // Program Management
    void loadPrograms();
    void uninstallSelected();
    void forceRemoveSelected();
    void filterPrograms(const QString& text);
    
    // File Shredder
    void browseFileToShred();
    void browseFolderToShred();
    void shredSelectedFile();
    void wipeFreeSpace();
    
    // Operation Control
    void pauseOperation();
    void cancelOperation();

    // Browser Cleaner
    void cleanBrowserData();

    // File Recovery Context Menu
    void showRecoveryContextMenu(const QPoint& pos);

    // Programs Context Menu
    void showProgramsContextMenu(const QPoint& pos);

private:
    void setupUi();
    void populateRecoveryTree();
    bool filterTreeItem(QTreeWidgetItem* item, const QString& filter);
    
    QTabWidget* tabWidget;
    QProgressBar* progressBar;
    QLabel* lblStatus; // Added for ETA and status messages

    // Tab 1: Programs
    QTableWidget* programsTable;
    QPushButton* btnRefreshPrograms;
    QPushButton* btnUninstall;
    QPushButton* btnForceRemove;
    QLineEdit* searchBox;

    // Tab 2: File Shredder
    QLabel* lblSelectedFile;
    QString currentFileToShred;
    QPushButton* btnBrowseFile;
    QPushButton* btnBrowseFolder;
    QComboBox* comboPasses;
    QPushButton* btnShred;
    QPushButton* btnPauseShred;
    QPushButton* btnCancelShred;
    QComboBox* comboDrives;
    QComboBox* comboWipeMode;
    QPushButton* btnWipeFreeSpace;
    QPushButton* btnPauseWipe;
    QPushButton* btnCancelWipe;

    // State
    ShredState m_shredState;

    // Tab 3: Browser Cleaner
    QCheckBox* chkChrome;
    QCheckBox* chkEdge;
    QCheckBox* chkFirefox;
    QCheckBox* chkHistory;
    QCheckBox* chkCookies;
    QCheckBox* chkCache;
    QPushButton* btnCleanBrowsers;

    // Tab 4: File Recovery
    QComboBox* comboRecoveryDrives;
    QPushButton* btnScanDrive;
    QLineEdit* searchRecoveryBox;
    QTreeWidget* recoveryTree;
    QPushButton* btnRecoverSelected;
    
    QList<RecoverableFile> m_recoverableFiles;

private slots:
    // File Recovery
    void scanRecoveryDrive();
    void recoverSelectedFile();
    void filterRecoveryFiles(const QString& text);
};
