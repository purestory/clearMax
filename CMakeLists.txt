cmake_minimum_required(VERSION 3.16)
project(clearMax VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt 6 (fallback to Qt 5 if 6 is not found)
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Core Gui)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Core Gui)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Source files
set(SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/FileShredder.cpp
    src/FileShredder.h
    src/RegistryMgr.cpp
    src/RegistryMgr.h
    src/BrowserCleaner.cpp
    src/BrowserCleaner.h
    clearMax.rc
)

add_executable(clearMax WIN32 ${SOURCES})

# UAC Manifest via Linker Flags
if(WIN32 AND MSVC)
    set_target_properties(clearMax PROPERTIES LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")
endif()

target_link_libraries(clearMax PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Gui)

# Windows API libraries needed for registry and shell execution
if(WIN32)
    target_link_libraries(clearMax PRIVATE Advapi32 Shell32 User32 Shlwapi)
endif()
