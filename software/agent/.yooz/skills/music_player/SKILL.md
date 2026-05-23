---
name: 音乐播放器
description: 当用户要求播放音乐、听歌、播放歌曲、停止播放、切换歌曲时触发
---

## 音乐播放路由

### 默认：在电脑上播放（hermes_tool）
用户没有指定播放位置时，默认通过 hermes_tool 在 Windows 电脑上播放：
- 播放音乐：hermes_tool {"intent": "打开网易云音乐并播放", "category": "music"}
- 暂停/停止：hermes_tool {"intent": "停止音乐播放", "category": "music"}
- 切歌：hermes_tool {"intent": "切换到下一首歌", "category": "music"}

### 用户明确说"本地"时：在树莓派上播放（bash）
仅在用户说"本地播放"、"在树莓派上播放"时使用 bash：
1. 安装（仅首次）：sudo apt-get install -y mpg123 alsa-utils
2. 播放：pkill mpg123; mpg123 ~/music/*.mp3 &
3. 停止：pkill mpg123

注意：bash 播放命令必须加 & 后台运行。
