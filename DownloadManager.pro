# DownloadManager.pro
TEMPLATE = app
TARGET = DownloadManager
VERSION = 1.6.3

# 使用 C++17 标准
CONFIG += c++17
CONFIG += automoc autouic autorcc

# Qt 模块
QT += core widgets network concurrent

# 源文件
SOURCES += \
    src/main.cpp \
    src/mainwindow.cpp \
    src/downloadmanager.cpp \
    src/downloaditem.cpp \
    src/downloadworker.cpp \
    src/downloadsegment.cpp \
    src/preferences.cpp \
    src/downloadutils.cpp \
    src/preferencesdialog.cpp \
    src/aboutdialog.cpp

# 头文件
HEADERS += \
    src/mainwindow.h \
    src/downloadmanager.h \
    src/downloaditem.h \
    src/downloadworker.h \
    src/downloadsegment.h \
    src/preferences.h \
    src/downloadutils.h \
    src/preferencesdialog.h \
    src/downloadstatus.h \
    src/aboutdialog.h

# 包含路径
INCLUDEPATH += src

# 资源文件（如果存在）
exists(resources.qrc) {
    RESOURCES += resources.qrc
}

# 线程库（Unix 系统需要显式链接，Windows 通常不需要）
unix:!macx {
    LIBS += -lpthread
}

# Linux 下安装桌面文件（Windows 忽略）
unix {
    target.path = /usr/local/bin
    INSTALLS += target

    desktop.path = /usr/share/applications
    desktop.files = download-manager.desktop
    INSTALLS += desktop

    icon.path = /usr/share/icons/hicolor
    icon.files = icons/hicolor
    INSTALLS += icon
}