[中文](./README_CN.md)

# YooZi

> A voice-activated smart assistant for Raspberry Pi, with camera streaming and remote monitoring.

<p align="center">
  <img src="assets/front_view.JPG" alt="YooZi Front View" width="45%" />
  <img src="assets/side_view.JPG" alt="YooZi Side View" width="45%" />
</p>

<p align="center">
  <img src="assets/installation.JPG" alt="Installation" width="60%" />
</p>

## System Architecture

YooZi consists of three software components and a 3D-printable enclosure:

| Component | Description |
|---|---|
| **agent** | C++ voice assistant with wake-word activation, ASR/TTS, and tool calling |
| **camera-stream** | HTTP camera streaming service (MJPEG) |
| **monitor-app** | Flutter app for remote camera monitoring |

---

## Agent — Build & Run

### Prerequisites

```bash
sudo apt install libasound2-dev libcurl4-openssl-dev cmake build-essential pkg-config libwebsockets-dev
```

### Build

```bash
cd software/agent
mkdir -p build && cd build
cmake ..
make
```

### Run

```bash
export ZHIPU_API_KEY="your_api_key"
export ALIBABA_NLS_APPKEY="your_appkey"
export ALIBABA_NLS_TOKEN="your_token"
./yoozi
```

### Environment Variables

| Variable | Required | Description |
|---|---|---|
| `ZHIPU_API_KEY` | Yes | Zhipu GLM API key |
| `ALIBABA_NLS_APPKEY` | Yes | Alibaba Cloud NLS app key |
| `ALIBABA_NLS_TOKEN` | Yes | Alibaba Cloud NLS token |
| `CAMERA_STREAM_URL` | No | Camera service URL (default: `http://127.0.0.1:8080`) |

### Wake Words

- **Wake**: "你好柚子" / "柚子"
- **Sleep**: "退下" / "滚吧" / "你下去吧" / "下去吧"

---

## Camera Stream — Build & Run

### Prerequisites

```bash
sudo apt install cmake build-essential libopencv-dev
```

### Build

```bash
cd software/camera-stream
mkdir -p build && cd build
cmake ..
make
```

### Run

```bash
# With libcamera V4L2 compatibility (recommended for Raspberry Pi CSI cameras)
./start.sh --port 8080 --width 640 --height 480

# Or direct execution
./camera-stream --port 8080 --device /dev/video0
```

### API Endpoints

| Endpoint | Description |
|---|---|
| `GET /` | JSON status |
| `GET /snapshot` | Single JPEG frame |
| `GET /stream` | MJPEG multipart stream |

---

## Monitor App — Usage

A Flutter mobile app for viewing the camera stream remotely.

### Prerequisites

- Flutter SDK >= 3.0

### Build & Run

```bash
cd software/monitor-app
flutter pub get
flutter run
```

### Usage

1. Enter the IP address and port of the camera-stream service
2. Tap **Connect** to start viewing
3. Use the fullscreen button for immersive viewing
4. Auto-reconnects on connection loss

---

## Hardware

3D-printable enclosure files located in the `hardware/` directory:

| File | Description |
|---|---|
| `main_frame.3mf` | Main body frame |
| `top_cover.3mf` | Top cover |
| `bottom_cover.3mf` | Bottom cover |
| `back_cover.3mf` | Back cover |
| `left_handle.3mf` | Left handle |
| `right_handle.3mf` | Right handle |

---

## License

[MIT License](LICENSE) &copy; 2026 班小吉
