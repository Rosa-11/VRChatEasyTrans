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

    emit debug(QString("AudioCapture: vadThreshold=%1, silenceDuration=%2ms, maxSilenceFrames=%3")
                   .arg(m_vadThreshold, 0, 'f', 4)
                   .arg(m_minSilenceDurationMs)
                   .arg(m_maxSilenceFrames));

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
        emit debug(QString("AudioCapture: device '%1' not found, using default: %2")
                       .arg(deviceName, selectedDevice.description()));
    } else {
        emit debug(QString("AudioCapture: using device: %1").arg(selectedDevice.description()));
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
    emit debug("AudioCapture initialized and started");
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
    emit debug("AudioCapture stopped");
}

// ─────────────────────────────────────────────────────────────────────────────
// resetState()
// ─────────────────────────────────────────────────────────────────────────────
void AudioCapture::resetState()
{
    m_state             = RecordingState::Idle;
    m_bufferQueue.clear();
    m_silenceFrameCount = 0;
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

    qDebug() << QString("[VAD] RMS: %1 | Voice: %2 | State: %3 | SilenceCnt: %4")
                    .arg(rms, 0, 'f', 4)
                    .arg(hasVoice ? "YES" : "NO ")
                    .arg(static_cast<int>(m_state))
                    .arg(m_silenceFrameCount);

    switch (m_state) {

    // ── Idle：等待第一帧语音 ─────────────────────────────────────────────────
    case RecordingState::Idle:
        if (hasVoice) {
            m_state = RecordingState::Buffering;
            m_bufferQueue.clear();
            m_bufferQueue.append(frame);
            m_silenceFrameCount = 0;
            emit debug("VAD: voice detected, start buffering...");
        }
        break;

    // ── Buffering：预积累阶段，达到阈值才触发识别 ────────────────────────────
    case RecordingState::Buffering:
        if (hasVoice) {
            m_bufferQueue.append(frame);
            m_silenceFrameCount = 0;

            if (m_bufferQueue.size() >= MIN_FRAMES_TO_TRIGGER) {
                // 达到触发阈值：先发 startRecognition，再逐帧发音频
                emit startRecognition();
                emit debug(QString("VAD: triggered! sending %1 buffered frames")
                               .arg(m_bufferQueue.size()));
                for (const QByteArray &f : m_bufferQueue)
                    emit sendAudioChunk(f);
                m_bufferQueue.clear();
                m_state             = RecordingState::Recording;
                m_silenceFrameCount = 0;
            }
        } else {
            // 语音开头短暂静音容忍（最多 3 帧 = 120ms）
            // 超过则认为是噪音，丢弃缓冲回到 Idle
            m_silenceFrameCount++;
            if (m_silenceFrameCount > 3) {
                m_bufferQueue.clear();
                m_silenceFrameCount = 0;
                m_state = RecordingState::Idle;
                emit debug("VAD: noise discarded, back to Idle");
            }
        }
        break;

    // ── Recording：持续发送阶段 ──────────────────────────────────────────────
    case RecordingState::Recording:
        if (hasVoice) {
            m_silenceFrameCount = 0;
            emit sendAudioChunk(frame);
        } else {
            // 静音帧不发给讯飞，仅累计静音帧数
            m_silenceFrameCount++;
            if (m_silenceFrameCount >= m_maxSilenceFrames) {
                // 静音持续超过断句时长：通知 SpeechRecogniser 发尾帧
                emit stopRecognition();
                emit debug(QString("VAD: silence timeout (%1ms), stop recognition")
                               .arg(m_silenceFrameCount * FRAME_MS));
                m_state             = RecordingState::Idle;
                m_silenceFrameCount = 0;
            }
        }
        break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// onTimerTimeout() — 定时器回调（每 40ms 触发一次）
//
// 【核心修复：音频读取方式从"定时读固定长度"改为"积累缓冲区 + 切帧"】
//
// ❌ 原来的问题：
//   每次定时器触发直接 read(1280)，期望恰好读到 1280 字节。
//   但 QAudioSource 的硬件采集节奏与 QTimer 触发节奏不严格同步：
//   - 若定时器比硬件早触发：缓冲区不足 1280 字节，read() 返回不足量数据
//     或者全是零，RMS = 0，VAD 认为是静音，帧被丢弃
//   - 即使 VAD 偶然通过，发给讯飞的音频也大量夹杂零值静音帧
//   - 最终结果：讯飞收到的是无效音频，返回空字符串 w=""
//
// ✅ 现在的做法：
//   1. 每次触发时，把硬件缓冲区里所有已就绪数据一次性全部读出，
//      追加到内部积累缓冲区 m_accumBuffer。
//   2. 从 m_accumBuffer 里按 1280 字节切帧，每帧调用 processFrame()。
//   3. 不足一帧的数据留在 m_accumBuffer 等下次继续积累，绝不丢弃。
//
//   这样每一帧处理的都是真实采集到的 PCM 数据，不会有零值污染。
// ─────────────────────────────────────────────────────────────────────────────
void AudioCapture::onTimerTimeout()
{
    if (!m_audioDevice) return;

    // 步骤一：读取硬件缓冲区中所有已就绪的数据
    const qint64 available = m_audioDevice->bytesAvailable();
    if (available > 0) {
        const QByteArray newData = m_audioDevice->read(available);
        if (!newData.isEmpty()) {
            m_accumBuffer.append(newData);
        }
    }

    // 步骤二：从积累缓冲区切出完整帧逐帧处理
    while (m_accumBuffer.size() >= FRAME_SIZE) {
        const QByteArray frame = m_accumBuffer.left(FRAME_SIZE);
        m_accumBuffer.remove(0, FRAME_SIZE);
        processFrame(frame);
    }

    // 步骤三：m_accumBuffer 中剩余不足一帧的数据保留，等下次定时器触发继续积累
}
