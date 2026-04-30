cmake_minimum_required(VERSION 3.16)
project(DownloadManager VERSION 1.6.6 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network Concurrent)
find_package(Threads REQUIRED)

qt_standard_project_setup()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

qt_add_executable(DownloadManager
    src/main.cpp
    src/mainwindow.cpp

    src/downloadmanager.cpp
    src/downloaditem.cpp
    src/downloadworker.cpp
    src/downloadsegment.cpp
    src/preferences.cpp
    src/downloadutils.cpp
    src/preferencesdialog.cpp

    src/mainwindow.h
    src/downloadmanager.h
    src/downloaditem.h
    src/downloadworker.h
    src/downloadsegment.h
    src/preferences.h
    src/downloadutils.h
    src/preferencesdialog.h
    src/downloadstatus.h
    src/aboutdialog.h
    src/aboutdialog.cpp
)

target_include_directories(DownloadManager PRIVATE src)

target_link_libraries(DownloadManager PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    Qt6::Concurrent
    Threads::Threads
)

# 资源文件自动包含
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/resources.qrc)
    target_sources(DownloadManager PRIVATE resources.qrc)
endif()

# 安装 .desktop 文件
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/download-manager.desktop
        DESTINATION share/applications)

# 安装图标文件
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/icons/hicolor/
        DESTINATION share/icons/hicolor
        FILES_MATCHING PATTERN "*.png" PATTERN "*.svg")
