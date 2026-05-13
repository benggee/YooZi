# AEC、VAD 和 ASR 优化说明

## 概述

本项目已实现 Speex 回声消除（AEC）功能，并进行了多项优化以提升语音识别准确度和稳定性。

## 问题与解决方案

### 原始问题

1. **被自己的声音打断**：AEC 处理后的音频 `processed_buf` 被 VAD 忽略，VAD 仍然使用原始音频（包含回声）
2. **误触发 VAD**：在没说话时也会触发识别（如听到"杯子"、"电子"等）
3. **ASR 识别准确度低**：误识别"柚子"为"杯子"、"被子"等近音词

### 根本原因

AEC 处理后的音频 `processed_buf` 被 VAD 忽略，VAD 和 ASR 仍然使用原始音频（包含回声），导致所有 AEC 功能失效。

## 实现的修复

### Phase 1: 修复 AEC 音频流连接（关键修复）

**目标**：让 VAD 使用 AEC 处理后的音频而非原始音频

**修改**：
- `voice/alsa_audio_capture.hpp` 第 227 行：`barge_in_vad_.process(buf.data(), frames)` → `barge_in_vad_.process(processed_buf, frames)`
- `voice/alsa_audio_capture.hpp` 第 243 行：`vad_.process(buf.data(), frames)` → `vad_.process(processed_buf, frames)`

**影响**：修复后，VAD 将使用消除了回声的音频，大幅减少误触发。

### Phase 2: 调整 VAD 参数配置

**目标**：修正 barge-in 模式的阈值，使其比正常模式更高

**修改**：
- `voice/alsa_audio_capture.hpp` 第 34 行：`barge_in_vad_(sample_rate, 600.0f, 10, 40)` → `barge_in_vad_(sample_rate, 2000.0f, 15, 50)`

**参数说明**：
- 能量阈值：600 → 2000（提高 3.3 倍，只有大声说话才触发）
- 起始帧数：10 → 15（增加抗干扰能力）
- 结束帧数：40 → 50（更稳定的语音检测）

**影响**：播放期间只有真正的大声用户语音才会触发打断。

### Phase 3: 持续启用 AEC

**目标**：在所有模式下启用 AEC，而不只是播放期间

**修改**：
- `voice/alsa_audio_capture.hpp` 第 37 行：`aec_enabled_(false)` → `aec_enabled_(true)`
- `voice/alsa_audio_capture.hpp` 第 48-57 行：在 `start()` 方法中初始化 AEC
- `voice/voice_engine.hpp` 第 276 行：移除 `audio_capture_->setEchoCancellation(true);`
- `voice/voice_engine.hpp` 第 315 行：移除 `audio_capture_->setEchoCancellation(false);`

**影响**：所有音频采集都经过 AEC 处理，包括唤醒词检测。

### Phase 4: 改进 AEC 参考信号同步

**目标**：确保参考信号与实际扬声器播放同步

**修改**：
- `voice/alsa_audio_capture.hpp` 第 331-340 行：在 `provideEchoReference()` 中添加 100ms 初始延迟

**影响**：增加初始延迟补偿，减少参考信号超前播放的问题。

### Phase 5: 优化 flush() 的 AEC 处理

**目标**：避免 flush 时破坏 AEC 状态

**修改**：
- `voice/alsa_audio_capture.hpp` 第 71-81 行：移除 `flush()` 方法中的 AEC 缓冲区重置代码

**影响**：AEC 状态在 flush 时保持不变，避免内部状态不一致。

## 工作流程

1. **初始化 AEC**：`start()` 时自动初始化 AEC
2. **设置播放源**：`audio_capture_->setPlaybackSource(wav_path)`
3. **播放 TTS**：VoiceEngine 调用 `speak()` 方法
4. **后台线程**：读取 WAV 文件，逐帧提供参考信号（100ms 延迟后）
5. **实时处理**：每个音频帧执行 AEC 消除回声，VAD 使用 `processed_buf`
6. **持续运行**：AEC 在整个会话期间保持启用

## 使用方法

### 编译项目

```bash
cd /home/benge/app/YooZi/software/agent/build
make
```

### 运行程序

```bash
# 设置 API 密钥
export ZHIPU_API_KEY="your_api_key"
export ALIBABA_NLS_APPKEY="your_appkey"
export ALIBABA_NLS_TOKEN="your_token"

# 运行程序
./yoozi
```

### 测试验证

#### 1. 测试回声消除
- 启动程序
- 说"你好柚子"唤醒
- 观察是否还会被自己的语音打断
- **预期**：不再被自己的 TTS 语音触发

#### 2. 测试 VAD 误触发
- 启动程序，静置 5 分钟
- 检查日志是否仍有"听到: XXX"的误触发
- **预期**：大幅减少或完全消失

#### 3. 测试唤醒词识别
- 多次说"柚子"或"你好柚子"
- 检查识别准确度
- **预期**：减少"杯子"、"被子"等误识别

#### 4. 测试打断功能
- 唤醒助手
- 助手说话时大声喊"停"或"取消"
- **预期**：能正常打断播放，但不会被正常的助手语音打断

## 关键参数

```cpp
// 音频参数
const int sample_rate_ = 16000;        // 16kHz 采样率
const int frame_size_ = 160;           // 10ms 帧大小
const int filter_length = sample_rate_ * 2;  // 2秒回声长度
const int echo_buffer_size_ = sample_rate_ * 4;  // 4秒缓冲区

// VAD 参数
vad_(sample_rate, 800.0f, 8, 30)              // 正常模式：低阈值
barge_in_vad_(sample_rate, 2000.0f, 15, 50)  // Barge-in 模式：高阈值
```

## 性能影响

- **CPU 使用率**：AEC 处理增加约 5-10% CPU 使用率
- **延迟**：AEC 处理增加约 5-10ms 延迟（可忽略）
- **内存**：4 秒回声缓冲区约占用 128KB

## 故障排查

### 编译错误

- 确保已安装 `libspeexdsp-dev`：
  ```bash
  sudo apt install libspeexdsp-dev
  ```

### 回声未消除

1. 检查扬声器设备是否正确配置
2. 检查参考信号是否正确同步（100ms 延迟是否足够）
3. 检查 AEC 是否在所有模式下启用

### VAD 误触发

1. 检查是否使用了 `processed_buf` 而非 `buf.data()`
2. 调整 barge-in 模式的能量阈值
3. 检查环境噪声水平

### 识别准确度低

1. 检查 AEC 是否正常工作
2. 检查麦克风质量和位置
3. 检查 ASR 服务配置

## 扩展功能

项目结构支持扩展更高级的音频处理功能：

- 降噪（Denoise）
- 自动增益控制（AGC）
- 更复杂的回声消除算法（如 WebRTC AEC3）

如需添加这些功能，可以在 `AlsaAudioCapture` 类中相应的处理方法中实现。
