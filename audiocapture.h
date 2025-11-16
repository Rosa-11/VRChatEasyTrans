#ifndef AUDIOCAPTURE_H
#define AUDIOCAPTURE_H

#include <QObject>
#include <QAudioInput>
#include <QAudioDevice>
#include <qaudioformat.h>
#include <QAudioSource>
#include <QBuffer>
#include <QVector>
#include <QTimer>

class AudioCapture : public QObject
{
    Q_OBJECT

private:

    // 配置参数
    /*
     * 设备
    */
    QAudioDevice audioDevice;
    /*
     * VAD阈值，判断是否有语音活动的能量阈值。
     * 范围：0.0 ~ 1.0
     * 推荐：0.01 ~ 0.03
    */
    float m_vadThreshold;

    /*
     * 最小静音时长，用于判断用户是否说完一句话所需的连续静音时间，单位：ms
     * 400-600ms：说话快
     * 600-800ms：一般人
     * 800-1200ms：字字珠玑
    */
    int m_minSilenceDuration;

    /*
     * 音频采样率，单位：Hz
    */
    int m_sampleRate;

    // 暂停标志
    std::atomic<bool> m_shouldStop;

    // 音频数据缓冲区
    QVector<float> m_sentenceBuffer;


public:
    AudioCapture(int device_index);
    ~AudioCapture();

    bool cap();
    void stop();

    QVector<float>* getBuffer();

    // 配置参数的setter方法
    void setVadThreshold(float threshold) { m_vadThreshold = threshold; }
    void setMinSilenceDuration(int ms) { m_minSilenceDuration = ms; }

};

#endif // AUDIOCAPTURE_H
