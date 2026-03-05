#ifndef PREFERENCES_H
#define PREFERENCES_H

#include <QString>
#include <QSettings>

class Preferences
{
public:
    // 获取首选项（从 QSettings 读取）
    static QString getDefaultDownloadPath();
    static int getMaxConcurrentDownloads();
    static int getDefaultThreadCount();

    // 设置首选项（写入 QSettings）
    static void setDefaultDownloadPath(const QString &path);
    static void setMaxConcurrentDownloads(int count);
    static void setDefaultThreadCount(int count);

    static int getMaxRetries();
    static void setMaxRetries(int count);

private:
    static QSettings& settings();  // 获取全局 QSettings 对象
};

#endif // PREFERENCES_H
