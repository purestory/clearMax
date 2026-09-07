cmake_minimum_required(VERSION 3.16)
project(clearMax VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_definitions(UNICODE _UNICODE)
add_compile_options("/utf-8")

# Statically link the C++ runtime (Use /MT instead of /MD) so no DLLs are required
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Source files
set(SOURCES
    src/main.cpp
    src/FileShredder.cpp
    src/FileShredder.h
    src/RegistryMgr.cpp
    src/RegistryMgr.h
    src/BrowserCleaner.cpp
    src/BrowserCleaner.h
    src/NtfsRecovery.cpp
    src/NtfsRecovery.h
    src/FatRecovery.cpp
    src/FatRecovery.h
    src/RecoveryTypes.h
    src/TrayApp.cpp
    src/TrayApp.h
    src/MainWindow.cpp
    src/MainWindow.h
    clearMax.rc
)

add_executable(clearMax WIN32 ${SOURCES})

# UAC Manifest via Linker Flags
if(WIN32 AND MSVC)
    set_target_properties(clearMax PROPERTIES LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")
endif()

# Windows API libraries
if(WIN32)
    target_link_libraries(clearMax PRIVATE Advapi32 Shell32 User32 Shlwapi Comctl32 Ole32)
endif()
