#ifndef DOWNLOADSTATUS_H
#define DOWNLOADSTATUS_H

// 分片下载状态
enum class SegmentStatus {
    Waiting,    // 等待中（灰色）
    Downloading,// 下载中（绿色）
    Retrying,   // 重试中（黄色）
    Error,      // 错误（红色）
    Finished,   // 已完成（灰色）
    Canceled    // 已取消（灰色）
};

#endif // DOWNLOADSTATUS_H
