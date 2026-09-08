# clearMax

clearMax is a secure PC cleaning and file shredding utility designed for Windows. Built with modern C++ and the native Win32 API, it provides powerful, lightweight tools to permanently destroy sensitive data, wipe free space, and clean browser histories, ensuring your privacy remains intact.

## Features

### 1. Secure File Shredder
- **0-Pass (Fast Delete/SSD):** Truncates and scrambles the file name without overwriting sectors. Highly recommended for SSDs to save lifespan while obfuscating metadata.
- **1-Pass (Quick):** Overwrites files with zeros. Fast and secure against software recovery.
- **3-Pass (DoD 5220.22-M):** Overwrites files with random data 3 times. Meets the US Department of Defense standard.
- **7-Pass (Secure):** Overwrites files with random data 7 times for maximum security.
- **Smart SSD Detection:** Automatically detects if the target is on an SSD and downgrades to 1-Pass to protect the drive's lifespan while maintaining security.
- **Free Space Wiping:** Fills all empty space on a drive with cryptographic random data to permanently obfuscate previously deleted files. Includes a special **"MFT (Table) Only"** mode for SSDs that targets only the deleted file records (Master File Table) using dynamic background generation without harming the disk sectors.
- **Pause & Stop Controls:** Background processes support instant pause, resume, and cancellation.

### 2. Browser Cleaner
- Supports Google Chrome, Microsoft Edge, Mozilla Firefox, and Brave.
- Clears cache, cookies, browsing history, and download history.
- Safely terminates browser processes before cleaning to ensure complete removal.

### 3. Registry Manager
- View and uninstall installed programs safely.
- Async background processing ensures the UI remains fully responsive during slow uninstalls.

### 4. File Recovery
- **Multi-FileSystem Support:** Raw-level scanning and parsing for **NTFS**, **FAT32**, and **exFAT**.
- **Deep MFT & Cluster Scanning:** Bypasses Windows restrictions to directly parse Master File Tables and FAT cluster chains to identify deleted files (`0xE5` markers / `In-Use` flags).
- **Pause & Stop Controls:** Deep scanning supports instant pause, resume, and cancellation.
- Tree-view representation of the file system.
- Recover deleted files directly to a secure destination.

## Technical Details
- **Language:** C++17
- **Framework:** Native Win32 API (No external UI dependencies)
- **Build System:** CMake & MSVC (Visual Studio 2022)
- **OS:** Windows 10 / 11 (Requires Administrator Privileges for deep cleaning)

## Installation / Portable Usage
A standalone portable `.exe` is generated upon building, meaning you can simply run `clearMax.exe` without any installation or external dependencies.

## License
MIT License
