[English](./README.md)

# 柚子

> 运行在树莓派上的语音智能助手，支持摄像头实时流和远程监控。

<p align="center">
  <img src="assets/front_view.JPG" alt="正面视图" width="45%" />
  <img src="assets/side_view.JPG" alt="侧面视图" width="45%" />
</p>

<p align="center">
  <img src="assets/installation.JPG" alt="安装示意图" width="60%" />
</p>

## 系统架构

柚子由三个软件组件和一个 3D 打印外壳组成：

| 组件 | 说明 |
|---|---|
| **agent** | C++ 语音助手，支持唤醒词、语音识别/合成和工具调用 |
| **camera-stream** | HTTP 摄像头流媒体服务（MJPEG） |
| **monitor-app** | Flutter 远程摄像头监控应用 |

---

## Agent — 编译运行

### 依赖

```bash
sudo apt install libasound2-dev libcurl4-openssl-dev cmake build-essential pkg-config libwebsockets-dev
```

### 编译

```bash
cd software/agent
mkdir -p build && cd build
cmake ..
make
```

### 运行

```bash
export ZHIPU_API_KEY="your_api_key"
export ALIBABA_NLS_APPKEY="your_appkey"
export ALIBABA_NLS_TOKEN="your_token"
./yoozi
```

### 环境变量

| 变量 | 必需 | 说明 |
|---|---|---|
| `ZHIPU_API_KEY` | 是 | 智谱 GLM API 密钥 |
| `ALIBABA_NLS_APPKEY` | 是 | 阿里云 NLS 应用 Key |
| `ALIBABA_NLS_TOKEN` | 是 | 阿里云 NLS Token |
| `CAMERA_STREAM_URL` | 否 | 摄像头服务地址（默认：`http://127.0.0.1:8080`） |

### 唤醒词

- **唤醒**："你好柚子" / "柚子"
- **休眠**："退下" / "滚吧" / "你下去吧" / "下去吧"

---

## Camera Stream — 编译运行

### 依赖

```bash
sudo apt install cmake build-essential libopencv-dev
```

### 编译

```bash
cd software/camera-stream
mkdir -p build && cd build
cmake ..
make
```

### 运行

```bash
# 使用 libcamera V4L2 兼容层（推荐树莓派 CSI 摄像头）
./start.sh --port 8080 --width 640 --height 480

# 或直接运行
./camera-stream --port 8080 --device /dev/video0
```

### API 端点

| 端点 | 说明 |
|---|---|
| `GET /` | JSON 状态信息 |
| `GET /snapshot` | 单帧 JPEG 图片 |
| `GET /stream` | MJPEG 视频流 |

---

## Monitor App — 使用

Flutter 移动端应用，用于远程查看摄像头画面。

### 依赖

- Flutter SDK >= 3.0

### 编译运行

```bash
cd software/monitor-app
flutter pub get
flutter run
```

### 使用方法

1. 输入 camera-stream 服务的 IP 地址和端口
2. 点击 **Connect** 开始查看画面
3. 使用全屏按钮进入沉浸模式
4. 断线自动重连

---

## 硬件

3D 打印外壳文件位于 `hardware/` 目录：

| 文件 | 说明 |
|---|---|
| `main_frame.3mf` | 主体框架 |
| `top_cover.3mf` | 上盖 |
| `bottom_cover.3mf` | 下盖 |
| `back_cover.3mf` | 后盖 |
| `left_handle.3mf` | 左把手 |
| `right_handle.3mf` | 右把手 |

---

## 许可证

[MIT 许可证](LICENSE) &copy; 2026 班小吉
