#ifndef SPEECHRECOGNISER_H
#define SPEECHRECOGNISER_H

#include <QString>
#include <QVector>
#include <QUrl>
#include "ConfigManager.h"

class SpeechRecogniser
{
public:
    explicit SpeechRecogniser(const QString& id,
                              const QString& secret,
                              const QString& device_id = "BXC9WtrjbrD3BpLck3s6Xqf32GKEEkt4");

    void setApiKeys(const QString& id, const QString& secret);
    void setCuid(const QString& device_id);

    // 同步方法
    QString getAccessToken();
    QString recognizeSpeech(const QVector<float>& audio_data, int sample_rate = 16000, int channels = 1);

    // Getter方法
    QString getCurrentToken() const { return access_token; }
    QString getClientId() const { return client_id; }
    QString getCuid() const { return cuid; }

private:
    QByteArray audioToBase64(const QVector<float>& audio_data);
    QByteArray buildAccessTokenRequest();
    QByteArray buildRecognitionRequest(const QVector<float>& audio_data, int sample_rate, int channels);
    QString parseRecognitionResult(const QString& json_result);
    QByteArray httpPost(const QUrl& url, const QByteArray& data, const QString& contentType);

    // 音频数据验证和转换
    bool validateAudioData(const QVector<float>& audio_data);
    QVector<int16_t> convertFloatToInt16(const QVector<float>& audio_data);

private:
    ConfigManager& config;

    QString client_id;
    QString client_secret;
    QString access_token;
    QString cuid;
};

#endif // SPEECHRECOGNISER_H
