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
private:
    void processVAD(const QByteArray &data);
    float calculateRMS(const QByteArray &data);

    QAudioSource *m_audioSource = nullptr;
    QIODevice *m_audioDevice = nullptr;
    QAudioFormat m_format;

    // VAD 状态
    bool m_isSpeaking = false;
    int  m_silenceDuration = 0;          // 当前静音持续时间（ms）
    int  m_minSilenceDuration = 800;     // 判定为结束的静音时长（ms）
    double m_vadThreshold = 0.015;       // RMS 阈值（归一化）

    QTimer *m_silenceTimer = nullptr;
    static constexpr int TIMER_INTERVAL = 40;   // 40ms 检查一次

    // 音频缓冲（用于 VAD 检测）
    QByteArray m_buffer;
    static constexpr int BUFFER_SIZE = 3200;    // 100ms 16k 16bit 数据量

public:
    explicit AudioCapture(QObject *parent = nullptr);
    ~AudioCapture();

public slots:
    void initializeAndStart();
    void stop();

private slots:
    void start();               // 由initialize()调用

    void onAudioDataReady();
    void checkSilence();

signals:

    void audioDataReady(const QByteArray &pcmData);   // 实时音频数据（仅人声部分）
    void recordingStarted();
    void recordingFinished();                         // 静音超时后结束
    void vadStateChanged(bool isSpeaking);
    void error(const QString &message);
    void debug(const QString& debugMessage);
};

#endif // AUDIOCAPTURE_H
