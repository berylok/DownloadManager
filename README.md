# DownloadManager

**DownloadManager** 是一个基于 Qt6 开源框架开发的跨平台下载管理工具，支持多线程分段下载、断点续传、任务管理等功能。

下载管理器界面预览
<img width="728" height="604" alt="截图 2026-03-08 08-30-47" src="https://github.com/user-attachments/assets/da91fb59-bf79-4075-990a-80d2e0bc2d8f" />


## 功能特性

- 🚀 **多线程分段下载**：自动将文件分割为多个部分同时下载，提升下载速度
- ⏸️ **断点续传**：支持暂停/恢复下载任务
- 📋 **任务队列管理**：添加、删除、暂停、恢复下载任务
- 📊 **实时速度显示**：显示当前下载速度、进度和剩余时间
- ⚙️ **可配置选项**：自定义下载目录、最大并发数、线程数等
- 🌍 **多协议支持**：HTTP/HTTPS/FTP
- 🎨 **现代界面**：使用 Qt6 原生控件，支持深色/浅色主题

## 系统要求

- **操作系统**：Linux / Windows / macOS（本指南主要针对 Linux）
- **编译环境**：CMake 3.16+，支持 C++17 的编译器（GCC 8+ 或 Clang 8+）
- **Qt 版本**：Qt 6.0 或更高版本
- **可选依赖**：`qt6-gtk-platformtheme`（用于 Linux 下的 GTK 主题集成）

## 构建步骤

### 1. 安装依赖

#### Ubuntu / Debian
```bash
sudo apt update
sudo apt install cmake build-essential qt6-base-dev qt6-tools-dev qt6-tools-dev-tools
# 可选：GTK 主题支持（解决文件对话框问题）
sudo apt install qt6-gtk-platformtheme
