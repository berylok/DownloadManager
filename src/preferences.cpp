#include "preferences.h"
#include <QStandardPaths>
#include <QApplication>

QSettings& Preferences::settings()
{
    static QSettings s(QApplication::applicationDirPath() + "/config.ini", QSettings::IniFormat);
    return s;
}


QString Preferences::getDefaultDownloadPath()
{
    QString savedPath = settings().value("Download/Path").toString();
    if (!savedPath.isEmpty()) {
        return savedPath;
    }
    return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
}


int Preferences::getMaxConcurrentDownloads()
{
    return settings().value("Download/MaxConcurrent", 5).toInt();
}


int Preferences::getDefaultThreadCount()
{
    return settings().value("Download/ThreadCount", 20).toInt();
}


void Preferences::setDefaultDownloadPath(const QString &path)
{
    settings().setValue("Download/Path", path);
}


void Preferences::setMaxConcurrentDownloads(int count)
{
    settings().setValue("Download/MaxConcurrent", count);
}


void Preferences::setDefaultThreadCount(int count)
{
    settings().setValue("Download/ThreadCount", count);
}


int Preferences::getMaxRetries()
{
    return settings().value("Download/MaxRetries", 3).toInt(); // 默认 3
}


void Preferences::setMaxRetries(int count)
{
    settings().setValue("Download/MaxRetries", count);
}

