#pragma once
#include <QMainWindow>
#include <QTabWidget>
#include <QTableWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>

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

private:
    void setupUi();
    
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
};
