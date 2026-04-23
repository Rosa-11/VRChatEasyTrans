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
    void initialize();  // 从 ConfigManager 读取配置，打开音频设备，启动定时器
    void stop();        // 停止采集，释放资源

private slots:
    void onTimerTimeout();  // 定时器回调：读取硬件缓冲区数据并切帧

private:
    float calculateRMS(const QByteArray &data);   // 计算归一化 RMS 音量
    void  processFrame(const QByteArray &frame);  // 对一帧执行 VAD 状态机
    void  resetState();                           // 重置 VAD 状态机

private:
    // ─── 音频设备 ────────────────────────────────────────────────────────────
    QAudioSource *m_audioSource = nullptr;
    QIODevice    *m_audioDevice = nullptr;
    QAudioFormat  m_format;
    QTimer       *m_sampleTimer = nullptr;

    // ─── VAD 参数 ─────────────────────────────────────────────────────────────
    double m_vadThreshold;  // 音量阈值（归一化 RMS）
    int    m_minSilenceDurationMs;    // 断句最短静音时长（毫秒）

    // ─── VAD 状态机 ──────────────────────────────────────────────────────────
    enum class RecordingState {
        Idle,       // 空闲，等待语音
        Buffering,  // 预积累，还未达到触发阈值
        Recording   // 录制中，持续发送音频
    };
    RecordingState m_state = RecordingState::Idle;

    QList<QByteArray> m_bufferQueue;    // Buffering 状态下的预积累帧队列
    int m_silenceFrameCount  = 0;       // 连续静音帧计数
    int m_recordingFrameCount = 0;      // 当前句子已录制帧数（用于限制最长录制时长）

    // ─── 音频积累缓冲区（核心修复新增）──────────────────────────────────────
    // 用于积累从硬件读取的原始 PCM 数据，按 FRAME_SIZE 切帧后再做 VAD。
    // 解决定时器与硬件采集节奏不同步导致 read() 读到零值数据的问题。
    QByteArray m_accumBuffer;

    // ─── 常量 ────────────────────────────────────────────────────────────────
    static constexpr int FRAME_MS              = 40;    // 每帧时长（毫秒）
    static constexpr int FRAME_SIZE            = 1280;  // 每帧字节数（40ms@16kHz/16bit/1ch）
    static constexpr int MIN_FRAMES_TO_TRIGGER = 15;    // 触发识别所需最少连续语音帧
    static constexpr int MAX_RECORDING_FRAMES  = 1500;  // 单句最长录制帧数（60s）

    // 由 initialize() 根据配置动态计算，不在成员变量初始化时写死
    int m_maxSilenceFrames = 20;

signals:
    void startRecognition();
    void sendAudioChunk(const QByteArray &chunk);
    void stopRecognition();
    void error(const QString &message);
    void debug(const QString &message);
};

#endif // AUDIOCAPTURE_H
