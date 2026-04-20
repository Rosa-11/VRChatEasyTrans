#include "AudioCapture.h"
#include "ConfigManager.h"
#include <QMediaDevices>
#include <QAudioDevice>
#include <QDebug>
#include <cmath>

AudioCapture::AudioCapture(QObject *parent)
    : QObject(parent)
{
    m_silenceTimer = new QTimer(this);
    m_silenceTimer->setInterval(TIMER_INTERVAL);
    connect(m_silenceTimer, &QTimer::timeout, this, &AudioCapture::checkSilence);
}

AudioCapture::~AudioCapture()
{
    stop();
}

void AudioCapture::initializeAndStart()
{
    if(m_audioSource) delete m_audioSource;
    // 从配置读取参数
    ConfigManager &cfg = ConfigManager::getInstance();
    m_vadThreshold = cfg.getVadThreshold();
    m_minSilenceDuration = cfg.getMinSilenceDuration();

    // 设置音频格式
    m_format.setSampleRate(cfg.getSampleRate());
    m_format.setChannelCount(1);
    m_format.setSampleFormat(QAudioFormat::Int16);

    // 选择音频设备
    QAudioDevice device = QMediaDevices::defaultAudioInput();
    QList<QAudioDevice> deviceList = QMediaDevices::audioInputs();
    for(int i=0;i<deviceList.size();i++)
        if(deviceList[i].description() == cfg.getDevice())
            device = deviceList[i];
    if (device.isNull() || !device.isFormatSupported(m_format)) {
        emit error("No supported device");
        return;
    }

    m_audioSource = new QAudioSource(device, m_format, this);

    qDebug() <<"audioCapture initialize";

    start();
}

void AudioCapture::start()
{
    if (!m_audioSource) {
        emit error("Audio source not initialized");
        return;
    }

    m_isSpeaking = false;
    m_silenceDuration = 0;
    m_buffer.clear();

    m_audioDevice = m_audioSource->start();
    if (!m_audioDevice) {
        emit error("Failed to start audio capture");
        return;
    }

    connect(m_audioDevice, &QIODevice::readyRead, this, &AudioCapture::onAudioDataReady);
    m_silenceTimer->start();

    emit recordingStarted();
    qDebug() << "Audio capture started.";
}

void AudioCapture::stop()
{
    m_silenceTimer->stop();

    if (m_audioSource) {
        m_audioSource->stop();
    }

    if (m_audioDevice) {
        disconnect(m_audioDevice, &QIODevice::readyRead, this, &AudioCapture::onAudioDataReady);
        m_audioDevice = nullptr;
    }

    // 如果正在说话，强制结束
    if (m_isSpeaking) {
        m_isSpeaking = false;
        emit vadStateChanged(false);
        emit recordingFinished();
    }

    qDebug() << "Audio capture stopped.";
}

void AudioCapture::onAudioDataReady()
{
    if (!m_audioDevice) return;

    QByteArray data = m_audioDevice->readAll();
    if (data.isEmpty()) return;

    // VAD 处理
    processVAD(data);
}

void AudioCapture::processVAD(const QByteArray &data)
{
    float rms = calculateRMS(data);
    bool currentlySpeaking = (rms > m_vadThreshold);

    if (currentlySpeaking) {
        // 有声音：重置静音计时
        m_silenceDuration = 0;

        if (!m_isSpeaking) {
            m_isSpeaking = true;
            emit vadStateChanged(true);
            qDebug() << "VAD: speech started";
        }

        // 发送音频数据
        emit audioDataReady(data);
    } else {
        // 静音状态：数据不发送，仅用于 VAD 检测
        if (m_isSpeaking) {
            // 持续静音中，累加时间（由 checkSilence 定时器处理）
        }
    }

    // 缓存部分数据用于后续 VAD 细化（可选）
    // 这里简化处理，直接基于当前块判断
}

float AudioCapture::calculateRMS(const QByteArray &data)
{
    if (data.size() < 2) return 0.0f;

    const int16_t *samples = reinterpret_cast<const int16_t*>(data.constData());
    int sampleCount = data.size() / sizeof(int16_t);

    double sum = 0.0;
    for (int i = 0; i < sampleCount; ++i) {
        double sample = samples[i] / 32768.0;  // 归一化到 [-1, 1]
        sum += sample * sample;
    }

    return std::sqrt(sum / sampleCount);
}

void AudioCapture::checkSilence()
{
    if (!m_isSpeaking) return;

    m_silenceDuration += TIMER_INTERVAL;

    if (m_silenceDuration >= m_minSilenceDuration) {
        // 静音超时，结束本次识别
        m_isSpeaking = false;
        emit vadStateChanged(false);
        emit recordingFinished();
        qDebug() << "VAD: speech ended (silence timeout)";
        m_silenceDuration = 0;
    }
}
