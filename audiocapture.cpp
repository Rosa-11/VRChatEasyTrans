#include "AudioCapture.h"
#include "ConfigManager.h"
#include <QMediaDevices>
#include <QAudioDevice>
#include <cmath>
#include <QDebug>

// ─────────────────────────────────────────────────────────────────────────────
// 构造 / 析构
// ─────────────────────────────────────────────────────────────────────────────

AudioCapture::AudioCapture(QObject *parent)
    : QObject(parent)
{
    // 成员指针均已在头文件中用 =nullptr 初始化，构造函数无需额外操作
}

AudioCapture::~AudioCapture()
{
    // 析构时确保资源释放，stop() 内部已做空指针保护
    stop();
}

// ─────────────────────────────────────────────────────────────────────────────
// initialize() — 从配置读取参数，打开音频设备，启动采样定时器
// 由主窗口 __start__ 信号触发
// ─────────────────────────────────────────────────────────────────────────────
void AudioCapture::initialize()
{
    // 1. 从 ConfigManager 读取 VAD 参数
    ConfigManager &cfg = ConfigManager::getInstance();
    m_vadThreshold        = cfg.getVadThreshold();
    m_minSilenceDurationMs = cfg.getMinSilenceDuration();

    // 【修复】在这里重新计算，保证与最新配置同步
    m_maxSilenceFrames = m_minSilenceDurationMs / FRAME_MS;

    emit debug(QString("AudioCapture: vadThreshold=%.4f, silenceDuration=%1ms, maxSilenceFrames=%2")
                   .arg(m_minSilenceDurationMs)
                   .arg(m_maxSilenceFrames));

    // 2. 根据配置选择音频输入设备
    //    如果找不到配置的设备名，回退到系统默认设备
    QAudioDevice selectedDevice;
    const QString deviceName = cfg.getDevice();
    for (const QAudioDevice &d : QMediaDevices::audioInputs()) {
        if (d.description() == deviceName) {
            selectedDevice = d;
            break;
        }
    }
    if (selectedDevice.isNull()) {
        selectedDevice = QMediaDevices::defaultAudioInput();
        emit debug(QString("AudioCapture: device '%1' not found, using default: %2")
                       .arg(deviceName, selectedDevice.description()));
    } else {
        emit debug(QString("AudioCapture: using device: %1").arg(selectedDevice.description()));
    }

    // 3. 设置音频格式：16kHz / 16bit / 单声道（讯飞 API 要求）
    m_format.setSampleRate(16000);
    m_format.setChannelCount(1);
    m_format.setSampleFormat(QAudioFormat::Int16);

    // 检查设备是否支持该格式
    if (!selectedDevice.isFormatSupported(m_format)) {
        emit error("AudioCapture: audio format (16kHz/16bit/mono) not supported by device");
        return;
    }

    // 4. 创建 QAudioSource 并以 IO 模式启动（pull mode）
    //    start() 返回一个 QIODevice*，后续通过 read() 拉取数据
    m_audioSource = new QAudioSource(selectedDevice, m_format, this);
    m_audioDevice = m_audioSource->start();

    if (!m_audioDevice) {
        emit error("AudioCapture: failed to start audio source");
        return;
    }

    // 5. 启动采样定时器，每 FRAME_MS(40ms) 触发一次 onTimerTimeout
    m_sampleTimer = new QTimer(this);
    connect(m_sampleTimer, &QTimer::timeout,
            this, &AudioCapture::onTimerTimeout);
    m_sampleTimer->start(FRAME_MS);

    // 6. 重置 VAD 状态机，确保从干净状态开始
    resetState();

    emit debug("AudioCapture initialized and started");
}

// ─────────────────────────────────────────────────────────────────────────────
// stop() — 停止采样定时器和音频源
// 由主窗口 __stop__ 信号触发
// ─────────────────────────────────────────────────────────────────────────────
void AudioCapture::stop()
{
    // 停止定时器（空指针安全）
    if (m_sampleTimer) {
        m_sampleTimer->stop();
        // 不 delete，Qt 父子机制会在析构时清理
    }

    // 停止音频源
    if (m_audioSource) {
        m_audioSource->stop();
        // m_audioDevice 由 m_audioSource 管理，不需要单独 delete
        m_audioDevice = nullptr;
    }

    // 重置 VAD 状态
    resetState();

    emit debug("AudioCapture stopped");
}

// ─────────────────────────────────────────────────────────────────────────────
// resetState() — 将 VAD 状态机恢复到初始状态
// ─────────────────────────────────────────────────────────────────────────────
void AudioCapture::resetState()
{
    m_state             = RecordingState::Idle;
    m_bufferQueue.clear();
    m_silenceFrameCount = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// calculateRMS() — 计算一帧 PCM 16bit 音频的 RMS 值（归一化到 0.0~1.0）
// ─────────────────────────────────────────────────────────────────────────────
float AudioCapture::calculateRMS(const QByteArray &data)
{
    // PCM 16bit：每个采样点占 2 字节
    const int16_t *samples = reinterpret_cast<const int16_t*>(data.constData());
    const int count = data.size() / 2;

    if (count == 0) return 0.0f;

    // 计算均方根：sum(x²) / n 开根号
    double sumSquares = 0.0;
    for (int i = 0; i < count; ++i) {
        double s = static_cast<double>(samples[i]);
        sumSquares += s * s;
    }

    // 除以 32768（int16 最大值）归一化到 0.0~1.0
    return static_cast<float>(std::sqrt(sumSquares / count) / 32768.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// onTimerTimeout() — 核心 VAD 状态机，每 40ms 执行一次
//
// 状态转换图：
//
//   Idle ──[hasVoice]──→ Buffering ──[累积≥3帧]──→ Recording
//    ↑                       │                         │
//    └──[连续静音>3帧]────────┘         [静音帧数超限]──┘
//
// ─────────────────────────────────────────────────────────────────────────────
void AudioCapture::onTimerTimeout()
{
    if (!m_audioDevice) return;

    // 读取一帧音频（恰好 FRAME_SIZE = 1280 字节）
    QByteArray frame = m_audioDevice->read(FRAME_SIZE);
    if (frame.size() < FRAME_SIZE) {
        // 数据不足一帧，跳过（可能是设备刚启动或缓冲区未满）
        return;
    }

    // 计算当前帧的 RMS 音量并判断是否有语音
    const float rms      = calculateRMS(frame);
    const bool  hasVoice = (rms > static_cast<float>(m_vadThreshold));

    qDebug() << QString("[VAD] RMS: %1 | Voice: %2 | State: %3 | SilenceCnt: %4")
                    .arg(rms, 0, 'f', 4)
                    .arg(hasVoice ? "YES" : "NO ")
                    .arg(static_cast<int>(m_state))
                    .arg(m_silenceFrameCount);

    switch (m_state) {

    // ── Idle：等待第一帧语音 ─────────────────────────────────
    case RecordingState::Idle:
        if (hasVoice) {
            // 检测到语音，进入 Buffering 状态开始预积累
            m_state = RecordingState::Buffering;
            m_bufferQueue.clear();
            m_bufferQueue.append(frame);
            m_silenceFrameCount = 0;
            emit debug("VAD: voice detected, buffering...");
        }
        // 无语音：静置不动
        break;

    // ── Buffering：预积累阶段，达到阈值才触发识别 ────────────
    case RecordingState::Buffering:
        if (hasVoice) {
            // 有语音帧：继续累积，重置静音计数
            m_bufferQueue.append(frame);
            m_silenceFrameCount = 0;

            // 累积到足够帧数，触发识别
            if (m_bufferQueue.size() >= MIN_FRAMES_TO_TRIGGER) {
                // 1. 通知 SpeechRecogniser 开始一次识别会话
                emit startRecognition();
                emit debug(QString("VAD: triggered! sending %1 buffered frames")
                               .arg(m_bufferQueue.size()));

                // 2. 把缓冲里所有帧依次发出
                for (const QByteArray &f : m_bufferQueue)
                    emit sendAudioChunk(f);

                // 3. 切换到 Recording 状态，清空缓冲
                m_state = RecordingState::Recording;
                m_bufferQueue.clear();
                m_silenceFrameCount = 0;
            }
        } else {
            // 【修复】静音帧不立刻清空缓冲，而是累计计数：
            // 语音开头常有一两帧低于阈值（说话方式、麦克风响应延迟），
            // 连续超过 3 帧才认为是真正的噪音，此时才丢弃缓冲。
            m_silenceFrameCount++;
            if (m_silenceFrameCount > 3) {
                // 认定为短暂噪音，丢弃缓冲，回到 Idle
                m_bufferQueue.clear();
                m_silenceFrameCount = 0;
                m_state = RecordingState::Idle;
                emit debug("VAD: noise discarded, back to Idle");
            }
            // 静音不足 3 帧：保留缓冲，继续等待
        }
        break;

    // ── Recording：持续发送阶段 ──────────────────────────────
    case RecordingState::Recording:
        if (hasVoice) {
            // 有语音：重置静音计数，直接发送当前帧
            m_silenceFrameCount = 0;
            emit sendAudioChunk(frame);
        } else {
            // 静音帧：累计计数
            // 注意：静音帧不发送给讯飞——发送静音会浪费带宽，
            // 讯飞服务端有自己的 EOS 超时 (eos=10000ms)，
            // 我们通过 stopRecognition 主动发尾帧来结束识别。
            m_silenceFrameCount++;

            if (m_silenceFrameCount >= m_maxSilenceFrames) {
                // 静音持续超过断句时长：通知 SpeechRecogniser 发送尾帧
                emit stopRecognition();
                emit debug(QString("VAD: silence timeout (%1ms), stopping recognition")
                               .arg(m_silenceFrameCount * FRAME_MS));

                // 回到 Idle，等待下一句话
                m_state = RecordingState::Idle;
                m_silenceFrameCount = 0;
            }
            // 静音未超时：什么都不发，继续等待（语句内部停顿）
        }
        break;
    }
}
