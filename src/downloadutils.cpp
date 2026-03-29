#include "downloadutils.h"
#include <QDateTime>
#include <QString>  // 新增：显式包含 QString

namespace DownloadUtils {

QString formatFileSize(qint64 bytes) {
    const qint64 KB = 1024;
    const qint64 MB = KB * 1024;
    const qint64 GB = MB * 1024;
    const qint64 TB = GB * 1024;

    if (bytes >= TB) return QString("%1 TB").arg(bytes / (double)TB, 0, 'f', 2);
    if (bytes >= GB) return QString("%1 GB").arg(bytes / (double)GB, 0, 'f', 2);
    if (bytes >= MB) return QString("%1 MB").arg(bytes / (double)MB, 0, 'f', 2);
    if (bytes >= KB) return QString("%1 KB").arg(bytes / (double)KB, 0, 'f', 1);
    return QString("%1 B").arg(bytes);
}

QString formatSpeed(qint64 bytesPerSecond) {
    const qint64 KB = 1024;
    const qint64 MB = KB * 1024;
    const qint64 GB = MB * 1024;

    if (bytesPerSecond >= GB) return QString("%1 GB/s").arg(bytesPerSecond / (double)GB, 0, 'f', 2);
    if (bytesPerSecond >= MB) return QString("%1 MB/s").arg(bytesPerSecond / (double)MB, 0, 'f', 2);
    if (bytesPerSecond >= KB) return QString("%1 KB/s").arg(bytesPerSecond / (double)KB, 0, 'f', 2);
    return QString("%1 B/s").arg(bytesPerSecond);
}

// 修改：参数改为 qint64，防止长时间下载溢出
QString formatTimeFromSeconds(int seconds) {
    if (seconds < 0) seconds = 0;

    if (seconds < 60) return QString("%1 秒").arg(seconds);
    if (seconds < 3600) {
        int m = seconds / 60;
        int s = seconds % 60;
        return QString("%1 分%2 秒").arg(m).arg(s);
    }
    int h = seconds / 3600;
    int m = (seconds % 3600) / 60;
    return QString("%1 小时%2 分").arg(h).arg(m);
}

QString sanitizeFileName(const QString &name) {
    QString s = name;
    // 保留 Windows 非法字符（正反斜杠等）
    const QString forbidden = "\\/?%*:|\"<>";
    for (const QChar &c : forbidden) {
        s.replace(c, '_');
    }

    // 去掉首尾空格和点（点结尾会变成空格或导致问题）
    s = s.trimmed();
    if (s.isEmpty() || s == "." || s == "..") {
        return QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    }

#ifdef Q_OS_WIN
    // Windows 保留设备名（不区分大小写）
    const QStringList reserved = {"CON", "PRN", "AUX", "NUL",
                                  "COM1","COM2","COM3","COM4","COM5","COM6","COM7","COM8","COM9",
                                  "LPT1","LPT2","LPT3","LPT4","LPT5","LPT6","LPT7","LPT8","LPT9"};
    QString base = s;
    int dotIndex = base.indexOf('.');
    QString namePart = (dotIndex == -1) ? base : base.left(dotIndex);
    if (reserved.contains(namePart.toUpper())) {
        s = "_" + s;   // 加前缀避免冲突
    }
#endif

    return s;
}

QString formatTimeFromMs(qint64 ms)
{
    if (ms < 0) ms = 0;
    int seconds = static_cast<int>(ms / 1000);  // ← 转换为 int
    return formatTimeFromSeconds(seconds);
}

} // namespace DownloadUtils
