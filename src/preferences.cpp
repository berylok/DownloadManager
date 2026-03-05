#include "preferences.h"
#include <QStandardPaths>


QSettings& Preferences::settings()
{
    static QSettings s("", "DownloadManager"); // 公司名和应用名可自定义
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
    return settings().value("Download/ThreadCount", 4).toInt();
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

