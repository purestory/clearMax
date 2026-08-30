#include "MainWindow.h"
#include <QApplication>
#include <QMessageBox>
#include <windows.h>

bool isRunAsAdmin() {
    BOOL fIsRunAsAdmin = FALSE;
    DWORD dwError = ERROR_SUCCESS;
    PSID pAdministratorsGroup = NULL;

    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&NtAuthority, 2,
        SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0,
        &pAdministratorsGroup)) {
        
        CheckTokenMembership(NULL, pAdministratorsGroup, &fIsRunAsAdmin);
        FreeSid(pAdministratorsGroup);
    }
    return fIsRunAsAdmin == TRUE;
}

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);
    
    // It's good practice to verify admin rights at runtime even if the manifest requests it
    if (!isRunAsAdmin()) {
        QMessageBox::critical(nullptr, "Administrator Privileges Required", 
            "ClearMax requires Administrator privileges to access registry keys and perform secure file deletions on system drives.\n\nPlease restart the application as Administrator.");
        return 1;
    }

    MainWindow w;
    w.show();
    return a.exec();
}
