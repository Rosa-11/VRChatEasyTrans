#include "AudioCapture.h"
#include <QMediaDevices>
#include <QAudioDevice>
#include <QDebug>
#include <QList>
#include <QFile>
#include <QScopedPointer>
#include <cmath>

AudioCapture::AudioCapture(int device_index){
    this->m_vadThreshold = 0.01f;
    this->m_minSilenceDuration = 800;
    this->m_sampleRate = 16000;

    this->m_sentenceBuffer.reserve(m_sampleRate * 60);
    this->m_shouldStop = false;

    QList<QAudioDevice> list = QMediaDevices::audioInputs();
    if (device_index >= 0 && device_index < list.size()) {
        this->audioDevice = list[device_index];
        qDebug() << "Selected audio device:" << audioDevice.description();
    } else {
        this->audioDevice = QMediaDevices::defaultAudioInput();
    }
}

AudioCapture::~AudioCapture(){
    stop();
}

QVector<float>* AudioCapture::getBuffer(){
    return &m_sentenceBuffer;
}

bool AudioCapture::cap()
{
    m_sentenceBuffer.clear();
    m_shouldStop.store(false);

    QAudioFormat format;
    format.setSampleRate(m_sampleRate);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    if (!audioDevice.isFormatSupported(format)) {
        format = audioDevice.preferredFormat();
        m_sampleRate = format.sampleRate();
    }

    QScopedPointer<QAudioSource> audioInput(new QAudioSource(audioDevice, format));
    QBuffer buffer;

    if (!buffer.open(QIODevice::ReadWrite)) {
        qWarning() << "Failed to open buffer";
        return false;
    }

    // VAD状态变量
    bool isSpeaking = false;
    bool hasStarted = false;
    int silenceCounter = 0;
    int speechCounter = 0;
    const int speechStartThreshold = 3;
    qint64 lastPos = 0;

    qDebug() << "Listening... (Speak now)";

    audioInput->start(&buffer);

    QEventLoop loop;
    QTimer processTimer;
    processTimer.setInterval(50);

    QObject::connect(&processTimer, &QTimer::timeout, [&]() {
        // 检查中断标志
        if (m_shouldStop.load()) {
            qDebug() << "Recording interrupted by user";
            processTimer.stop();
            audioInput->stop();
            buffer.close();
            loop.quit();
            return;
        }

        qint64 bufferSize = buffer.size();
        qint64 newDataSize = bufferSize - lastPos;

        if (newDataSize > 0) {
            qint64 savedPos = buffer.pos();
            buffer.seek(lastPos);
            QByteArray newData = buffer.read(newDataSize);
            buffer.seek(savedPos);
            lastPos = bufferSize;

            if (newData.size() > 0) {
                const int16_t* samples = reinterpret_cast<const int16_t*>(newData.constData());
                int sampleCount = newData.size() / sizeof(int16_t);

                // VAD分析
                int speechSampleCount = 0;
                float maxEnergy = 0.0f;
                float totalEnergy = 0.0f;

                for (int i = 0; i < sampleCount; ++i) {
                    float sample = static_cast<float>(samples[i]) / 32768.0f;
                    float energy = std::abs(sample);
                    totalEnergy += energy;

                    if (energy > maxEnergy) maxEnergy = energy;
                    if (energy > m_vadThreshold) speechSampleCount++;
                }

                float avgEnergy = totalEnergy / sampleCount;
                float speechRatio = static_cast<float>(speechSampleCount) / sampleCount;

                bool hasSpeech = speechRatio > 0.08f && maxEnergy > m_vadThreshold * 1.5;

                if (hasSpeech) {
                    silenceCounter = 0;
                    speechCounter++;

                    if (!hasStarted && speechCounter >= speechStartThreshold) {
                        hasStarted = true;
                        isSpeaking = true;
                        qDebug() << "Speech detected! Recording...";
                    } else if (hasStarted) {
                        isSpeaking = true;
                    }
                } else {
                    speechCounter = 0;
                    if (hasStarted && isSpeaking) {
                        silenceCounter += 50;
                    }
                }

                // 保存录音数据
                if (hasStarted) {
                    for (int i = 0; i < sampleCount; ++i) {
                        float sample = static_cast<float>(samples[i]) / 32768.0f;
                        m_sentenceBuffer.append(sample);
                    }

                    // 每2秒显示进度
                    if (m_sentenceBuffer.size() % (m_sampleRate * 2) == 0) {
                        float duration = m_sentenceBuffer.size() / static_cast<float>(m_sampleRate);
                        qDebug() << "Recording..." << QString::number(duration, 'f', 1) << "s, Silence:" << silenceCounter << "ms";
                    }
                }

                // 检查停止条件
                if (hasStarted && isSpeaking && silenceCounter >= m_minSilenceDuration) {
                    float duration = m_sentenceBuffer.size() / static_cast<float>(m_sampleRate);
                    qDebug() << "Recording complete:" << QString::number(duration, 'f', 1) << "seconds";

                    processTimer.stop();
                    audioInput->stop();
                    buffer.close();
                    loop.quit();
                    return;
                }
            }
        }

        // 保护机制：最长录制30秒
        if (hasStarted && m_sentenceBuffer.size() > m_sampleRate * 30) {
            qDebug() << "Maximum recording time reached (30s)";
            processTimer.stop();
            audioInput->stop();
            buffer.close();
            loop.quit();
        }
    });

    processTimer.start();
    loop.exec();

    processTimer.stop();

    if (audioInput->state() != QAudio::StoppedState) {
        audioInput->stop();
    }
    if (buffer.isOpen()) {
        buffer.close();
    }

    return !m_sentenceBuffer.isEmpty();
}

void AudioCapture::stop()
{
    m_shouldStop.store(true);
}
