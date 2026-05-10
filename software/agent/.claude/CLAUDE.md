# Mose — 树莓派语音智能助手

C++11 语音交互 Agent，运行在树莓派上，通过唤醒词激活，支持连续对话、工具调用和语音播报。

## 项目结构

```
cmd/main.cpp                      入口，初始化所有组件，启动 VoiceEngine
CMakeLists.txt                    构建配置（C++11, libcurl, libasound）

schema/message.hpp                 核心数据类型：Role, Message, ToolCall, ToolResult, ToolDefinition

provider/provider.hpp              LLMProvider 接口
provider/openai_provider.hpp       OpenAI 兼容协议实现（用于智谱 GLM）
provider/claude_provider.hpp       Anthropic 协议实现（备用）

tools/base_tool.hpp                BaseTool 接口（name, definition, execute）
tools/registry.hpp                 工具注册与执行
tools/read_file_tool.hpp           读文件工具
tools/camera_tool.hpp              拍照工具（base64 输出）
tools/speech_to_text_tool.hpp      语音转文字工具
tools/text_to_speech_tool.hpp      文字转语音工具

camera/camera_capture.hpp          CameraCapture 接口
camera/pi_camera_capture.hpp       树莓派摄像头（libcamera-jpeg / fswebcam）

speech/speech_recognizer.hpp       SpeechRecognizer 接口
speech/speech_synthesizer.hpp      SpeechSynthesizer 接口
speech/alibaba_config.hpp          阿里云 NLS 凭证（ALIBABA_NLS_APPKEY, ALIBABA_NLS_TOKEN）
speech/alibaba_speech_recognizer.hpp  阿里云 ASR 实现
speech/alibaba_speech_synthesizer.hpp 阿里云 TTS 实现

voice/audio_capture.hpp            AudioCapture 接口
voice/alsa_audio_capture.hpp       ALSA 麦克风采集 + VAD + WAV 写入（后台线程）
voice/vad.hpp                      能量检测语音活动检测器（Voice Activity Detector）
voice/wav_writer.hpp               PCM → WAV 文件写入
voice/voice_engine.hpp             语音引擎状态机（SLEEPING / AWAKE）

engine/agent_engine.hpp            文本模式 Agent 引擎（备用）

lib/anthropic/                     Anthropic C++ SDK 客户端（header-only）
lib/vendor/nlohmann/json.hpp       JSON 库
lib/vendor/base64.hpp              Base64 编码器
```

## 核心逻辑

### VoiceEngine 状态机

```
SLEEPING → (唤醒词 "Mose") → AWAKE → (2分钟无对话) → SLEEPING
```

1. **SLEEPING**：麦克风持续采集，VAD 检测到语音 → 保存 WAV → ASR 转文字 → 检查唤醒词（"mose"/"摩丝"/"莫斯"/"摩斯"）
2. **AWAKE**：等待用户说话 → ASR → 发送给 LLM（带工具列表）→ LLM 返回文本或工具调用 → 执行工具 → 循环直到得到最终文本 → TTS → `aplay -D card3` 播放
3. 播放期间麦克风自动静音（`setMuted(true)`），防止回声

### 工具调用流程

LLM 返回 `tool_calls` 时，VoiceEngine 循环执行：调用 `Registry::execute()` → 获取结果 → 作为 observation 送回 LLM → 直到 LLM 只返回文本（无工具调用）。

### ASR/TTS 接口设计

`SpeechRecognizer` 和 `SpeechSynthesizer` 是纯虚接口，阿里云是当前实现。后续可新增智谱、腾讯实现，只需添加新类，不改动接口和工具代码。

### 音频采集

`AlsaAudioCapture` 在后台线程持续读取 ALSA PCM 数据，喂给 VAD。VAD 通过能量阈值检测语音起止点，语音结束时保存 WAV 并通知主线程（mutex + condition_variable）。

## 环境变量

| 变量 | 用途 |
|---|---|
| `ZHIPU_API_KEY` 或 `OPENAI_API_KEY` | 智谱 GLM API 密钥（必需） |
| `ALIBABA_NLS_APPKEY` | 阿里云 NLS 应用 Key（语音功能需要） |
| `ALIBABA_NLS_TOKEN` | 阿里云 NLS Token（语音功能需要） |

## 构建（树莓派）

```bash
sudo apt install libasound2-dev libcurl4-openssl-dev cmake build-essential
cd build && cmake .. && make
```

## API 端点

- **LLM**：`https://open.bigmodel.cn/api/paas/v4/chat/completions`（智谱 GLM，OpenAI 兼容协议）
- **ASR**：`https://nls-gateway-cn-shanghai.aliyuncs.com/stream/v1/asr`
- **TTS**：`https://nls-gateway-cn-shanghai.aliyuncs.com/stream/v1/tts`

## 仓库

- GitHub：`https://github.com/benggee/moss`（prod 分支）
