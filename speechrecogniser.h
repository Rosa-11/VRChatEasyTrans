#ifndef SPEECHRECOGNISER_H
#define SPEECHRECOGNISER_H

#include <QObject>
#include <QVector>
#include "configmanager.h"

class SpeechRecogniser : public QObject
{
    Q_OBJECT

public:
    SpeechRecogniser(QObject* parent = nullptr);
    ~SpeechRecogniser();

    QString recognizeSpeech(const QVector<float>& audio_data, int sample_rate = 16000, int channels = 1);

private:
    ConfigManager& config;

    QVector<int16_t> convertFloatToInt16(const QVector<float>& audio_data);
    QString generateAuthorizationHeader();
    void sendAudioData(class QWebSocket* webSocket, const QVector<float>& audio_data);
};

#endif // SPEECHRECOGNISER_H
