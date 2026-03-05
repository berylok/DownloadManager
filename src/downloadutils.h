// downloadutils.h
#ifndef DOWNLOADUTILS_H
#define DOWNLOADUTILS_H

#include <QString>
#include <QtGlobal>

namespace DownloadUtils {
QString formatFileSize(qint64 bytes);
QString formatSpeed(qint64 bytesPerSecond);
QString formatTimeFromSeconds(int seconds);
QString sanitizeFileName(const QString &name);
QString formatTimeFromMs(qint64 ms);
}

#endif // DOWNLOADUTILS_H
