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
}

AudioCapture::~AudioCapture()
{
    stop();
}

// ─────────────────────────────────────────────────────────────────────────────
// initialize()
// ─────────────────────────────────────────────────────────────────────────────
void AudioCapture::initialize()
{
    ConfigManager &cfg = ConfigManager::getInstance();
    m_vadThreshold         = cfg.getVadThreshold();
    m_minSilenceDurationMs = cfg.getMinSilenceDuration();
    m_maxSilenceFrames     = m_minSilenceDurationMs / FRAME_MS;

    emit debug(QString("AudioCapture: 触发阈值: %1, 断句时长: %2ms")
                   .arg(m_vadThreshold, 0, 'f', 4)
                   .arg(m_minSilenceDurationMs));

    // ── 选择音频输入设备 ─────────────────────────────────────────────────────
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
    }

    // ── 配置音频格式：16kHz / 16bit / 单声道（讯飞要求）────────────────────
    m_format.setSampleRate(16000);
    m_format.setChannelCount(1);
    m_format.setSampleFormat(QAudioFormat::Int16);

    if (!selectedDevice.isFormatSupported(m_format)) {
        emit error("AudioCapture: audio format (16kHz/16bit/mono) not supported by device");
        return;
    }

    // ── 创建 QAudioSource ─────────────────────────────────────────────────────
    m_audioSource = new QAudioSource(selectedDevice, m_format, this);

    // 【关键】设置足够大的硬件缓冲区（约 200ms = 6400 字节），
    // 防止定时器触发时缓冲区还没来得及积累满 1280 字节，导致 read() 返回零值数据。
    m_audioSource->setBufferSize(6400);

    // pull 模式启动：我们主动调用 read() 拉取数据
    m_audioDevice = m_audioSource->start();
    if (!m_audioDevice) {
        emit error("AudioCapture: failed to start audio source");
        return;
    }

    // 清空内部积累缓冲区
    m_accumBuffer.clear();

    // ── 启动采样定时器 ────────────────────────────────────────────────────────
    m_sampleTimer = new QTimer(this);
    connect(m_sampleTimer, &QTimer::timeout,
            this, &AudioCapture::onTimerTimeout);
    m_sampleTimer->start(FRAME_MS);

    resetState();
}

// ─────────────────────────────────────────────────────────────────────────────
// stop()
// ─────────────────────────────────────────────────────────────────────────────
void AudioCapture::stop()
{
    if (m_sampleTimer) {
        m_sampleTimer->stop();
    }
    if (m_audioSource) {
        m_audioSource->stop();
        m_audioDevice = nullptr;
    }
    m_accumBuffer.clear();
    resetState();
}

// ─────────────────────────────────────────────────────────────────────────────
// resetState()
// ─────────────────────────────────────────────────────────────────────────────
void AudioCapture::resetState()
{
    m_state               = RecordingState::Idle;
    m_bufferQueue.clear();
    m_silenceFrameCount   = 0;
    m_recordingFrameCount = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// calculateRMS() — 计算 PCM 16bit 音频帧的归一化 RMS 值（0.0 ~ 1.0）
// ─────────────────────────────────────────────────────────────────────────────
float AudioCapture::calculateRMS(const QByteArray &data)
{
    const int16_t *samples = reinterpret_cast<const int16_t*>(data.constData());
    const int      count   = data.size() / 2;
    if (count == 0) return 0.0f;

    double sumSquares = 0.0;
    for (int i = 0; i < count; ++i) {
        double s = static_cast<double>(samples[i]);
        sumSquares += s * s;
    }
    // 除以 32768.0 归一化到 [0.0, 1.0]
    return static_cast<float>(std::sqrt(sumSquares / count) / 32768.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// processFrame() — 对一个完整的 FRAME_SIZE 字节帧执行 VAD 状态机
// 从 onTimerTimeout() 中拆分出来，保持每个函数职责单一
// ─────────────────────────────────────────────────────────────────────────────
void AudioCapture::processFrame(const QByteArray &frame)
{
    const float rms      = calculateRMS(frame);
    const bool  hasVoice = (rms > static_cast<float>(m_vadThreshold));

    switch (m_state) {

    // ── Idle：等待第一帧语音 ─────────────────────────────────────────────────
    case RecordingState::Idle:
        if (hasVoice) {
            m_state = RecordingState::Buffering;
            m_bufferQueue.clear();
            m_bufferQueue.append(frame);
            m_silenceFrameCount = 0;
        }
        break;

    // ── Buffering：预积累阶段，达到阈值才触发识别 ────────────────────────────
    case RecordingState::Buffering:
        if (hasVoice) {
            m_bufferQueue.append(frame);
            m_silenceFrameCount = 0;

            if (m_bufferQueue.size() >= MIN_FRAMES_TO_TRIGGER) {
                emit startRecognition();
                emit debug("检测到语音");
                for (const QByteArray &f : m_bufferQueue)
                    emit sendAudioChunk(f);
                m_bufferQueue.clear();
                m_state               = RecordingState::Recording;
                m_silenceFrameCount   = 0;
                m_recordingFrameCount = MIN_FRAMES_TO_TRIGGER;
            }
        } else {
            m_silenceFrameCount++;
            if (m_silenceFrameCount > 3) {
                m_bufferQueue.clear();
                m_silenceFrameCount = 0;
                m_state = RecordingState::Idle;
            }
        }
        break;

    // ── Recording：持续发送阶段 ──────────────────────────────────────────────
    case RecordingState::Recording:

        emit sendAudioChunk(frame);
        
        if (hasVoice) {
            m_silenceFrameCount = 0;
        } else {
            m_silenceFrameCount++;
            if (m_silenceFrameCount >= m_maxSilenceFrames) {
                emit stopRecognition();
                emit debug("正在识别");
                m_state               = RecordingState::Idle;
                m_silenceFrameCount   = 0;
                m_recordingFrameCount = 0;
                return;
            }
        }

        // 超过最长录制时长60s，强制结束本句
        ++m_recordingFrameCount;
        if (m_recordingFrameCount >= MAX_RECORDING_FRAMES) {
            emit stopRecognition();
            emit debug("正在识别");
            m_state               = RecordingState::Idle;
            m_silenceFrameCount   = 0;
            m_recordingFrameCount = 0;
        }
        break;
    }
}

void AudioCapture::onTimerTimeout()
{
    if (!m_audioDevice) return;

    // 读取硬件缓冲区中所有已就绪的数据
    const qint64 available = m_audioDevice->bytesAvailable();
    if (available > 0) {
        const QByteArray newData = m_audioDevice->read(available);
        if (!newData.isEmpty()) {
            m_accumBuffer.append(newData);
        }
    }

    // 从积累缓冲区切出完整帧逐帧处理
    while (m_accumBuffer.size() >= FRAME_SIZE) {
        const QByteArray frame = m_accumBuffer.left(FRAME_SIZE);
        m_accumBuffer.remove(0, FRAME_SIZE);
        processFrame(frame);
    }
}
