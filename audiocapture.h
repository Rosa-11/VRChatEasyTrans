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
#include "ConfigManager.h"

class AudioCapture : public QObject
{
    Q_OBJECT

private:
    // 配置
    ConfigManager& config;
    // 设备
    QAudioDevice audioDevice;

    // 暂停标志
    std::atomic<bool> m_shouldStop;

    // 音频数据缓冲区
    QVector<float> m_sentenceBuffer;


public:
    AudioCapture(int device_index, QObject* parent);
    ~AudioCapture();

    bool cap();
    void stop();

    QVector<float>* getBuffer();

    // 配置参数的setter方法
    // void setVadThreshold(float threshold) { m_vadThreshold = threshold; }
    // void setMinSilenceDuration(int ms) { m_minSilenceDuration = ms; }

};

#endif // AUDIOCAPTURE_H
