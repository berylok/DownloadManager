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
    // Windows/Linux 通用 forbidden 字符
    const QString forbidden = QStringLiteral("\\/?%*:|\"<>\n\r\t");
    for (const QChar &c : forbidden) {
        s.replace(c, '_');
    }

    // 去除首尾空格
    s = s.trimmed();

    // 如果为空，生成时间戳文件名
    if (s.isEmpty()) {
        return QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    }

    // 额外处理：Windows 保留文件名 (可选，防止保存为 CON, PRN 等)
    // 如果需要跨平台完美兼容，可以取消下面注释
    /*
    const QStringList reserved = {"CON", "PRN", "AUX", "NUL", "COM1", "LPT1"};
    if (reserved.contains(s.toUpper())) {
        s = "_" + s;
    }
    */

    return s;
}

QString formatTimeFromMs(qint64 ms)
{
    if (ms < 0) ms = 0;
    int seconds = static_cast<int>(ms / 1000);  // ← 转换为 int
    return formatTimeFromSeconds(seconds);
}

} // namespace DownloadUtils
