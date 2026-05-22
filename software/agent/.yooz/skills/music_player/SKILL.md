---
name: 音乐播放器
description: 当用户要求播放音乐、安装音乐播放器、听歌、播放歌曲、停止播放时触发
---

## 安装（仅在首次使用或播放器未安装时执行）

检查并安装所需的播放器：
`sudo apt-get install -y mpg123 alsa-utils`

## 播放音乐

1. 先使用「音乐文件搜索」技能找到音频文件路径。

2. 使用 mpg123 播放，命令末尾必须加 `&` 将其转入后台运行：
   `mpg123 "/path/to/song.mp3" &`
   **注意**：bash 工具有 30 秒超时保护，播放音乐等常驻进程必须在命令末尾加 `&` 转入后台，否则会被系统强制终止。

3. 支持的播放命令示例：
   - MP3 文件：`mpg123 "/path/to/file.mp3" &`
   - WAV 文件：`aplay "/path/to/file.wav" &`
   - FLAC/OGG 等格式：`mpg123 "/path/to/file.flac" &`

## 停止播放

使用 pkill 终止播放进程：
`pkill mpg123` 或 `pkill aplay`

## 注意事项

- 播放命令必须加 `&` 后台运行，绝不可以在前台运行。
- 每次播放新歌前，先 `pkill mpg123` 停止上一首。
- 你拥有 sudo 权限，可以直接使用 sudo 安装软件。
