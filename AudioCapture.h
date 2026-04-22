#ifndef AUDIOCAPTURE_H
#define AUDIOCAPTURE_H

#include <QObject>
#include <QAudioSource>
#include <QAudioDevice>
#include <QByteArray>
#include <QTimer>

class AudioCapture : public QObject
{
    Q_OBJECT

public:
    explicit AudioCapture(QObject *parent = nullptr);
    ~AudioCapture();

public slots:
    // 初始化音频采集（从 ConfigManager 读取配置，启动采集定时器）
    void initialize();

    // 停止音频采集（停止定时器、停止音频源、重置状态）
    void stop();

private slots:
    // 每 40ms 触发一次，读取一帧音频并执行 VAD 状态机
    void onTimerTimeout();

private:
    // 计算一帧 PCM 数据的均方根音量（归一化到 0.0~1.0）
    float calculateRMS(const QByteArray &data);

    // 重置 VAD 状态机（回到 Idle，清空缓冲队列和计数器）
    void resetState();

private:
    // ─── 音频设备 ───────────────────────────────────────────
    QAudioSource *m_audioSource = nullptr;  // Qt 音频采集源
    QIODevice    *m_audioDevice = nullptr;  // 音频数据 IO 设备（由 m_audioSource->start() 返回）
    QAudioFormat  m_format;                 // 音频格式（16kHz / 16bit / 单声道）
    QTimer       *m_sampleTimer = nullptr;  // 采样定时器，每 FRAME_MS ms 触发一次

    // ─── VAD 参数（从 ConfigManager 读取）──────────────────
    double m_vadThreshold        = 0.015;   // 音量阈值（RMS 归一化值，超过则认为有语音）
    int    m_minSilenceDurationMs = 800;    // 断句所需最短静音时长（毫秒）

    // ─── VAD 状态机 ─────────────────────────────────────────
    enum class RecordingState {
        Idle,       // 空闲：等待检测到语音
        Buffering,  // 缓冲：已检测到语音，正在累积到触发阈值
        Recording   // 录制：已触发识别，持续发送音频分片
    };
    RecordingState m_state = RecordingState::Idle;

    // ─── 缓冲与计数 ─────────────────────────────────────────
    QList<QByteArray> m_bufferQueue;        // Buffering 状态的预积累队列
    int m_silenceFrameCount = 0;            // 连续静音帧计数（Recording 状态下用于断句）

    // ─── 常量 ───────────────────────────────────────────────
    static constexpr int FRAME_MS   = 40;   // 每帧时长（毫秒）
    static constexpr int FRAME_SIZE = 1280; // 每帧字节数（40ms × 16kHz × 16bit × 1ch = 1280 B）
    static constexpr int MIN_FRAMES_TO_TRIGGER = 10; // 触发识别所需最少帧数（10帧 = 400ms）

    // 【修复】不在成员变量处直接计算，而是在 initialize() 里根据配置动态赋值
    // 原写法 `= m_minSilenceDurationMs / FRAME_MS` 在构造时就算好了，
    // 之后 initialize() 更新 m_minSilenceDurationMs 时该值不会同步。
    int m_maxSilenceFrames = 20;            // 最大连续静音帧数（由 initialize() 根据配置计算）

signals:
    // 向 SpeechRecogniser 发送：开始识别（此后开始发送音频分片）
    void startRecognition();

    // 向 SpeechRecogniser 发送：一个 1280 字节的 PCM 音频分片
    void sendAudioChunk(const QByteArray &chunk);

    // 向 SpeechRecogniser 发送：停止识别（此后 SpeechRecogniser 发送尾帧）
    void stopRecognition();

    void error(const QString &message);
    void debug(const QString &message);
};

#endif // AUDIOCAPTURE_H
