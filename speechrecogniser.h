#ifndef SPEECHRECOGNISER_H
#define SPEECHRECOGNISER_H

#include <QObject>
#include <QWebSocket>
#include <QTimer>
#include <QByteArray>
#include <QQueue>

class SpeechRecogniser : public QObject
{
    Q_OBJECT
public:
    explicit SpeechRecogniser(QObject *parent = nullptr);
    ~SpeechRecogniser();

public slots:
    void initialize();
    void processAudioData(const QByteArray &data);
    void finishRecognition();

signals:
    void recognitionCompleted(const QString &finalText);
    void recognitionPartial(const QString &partialText);
    void socketStateChanged(const QString &state);
    void error(const QString &message);
    void debug(const QString &debugMessage);

private slots:
    void onConnected();
    void onTextMessageReceived(const QString &message);
    void onDisconnected();
    void onError(QAbstractSocket::SocketError error);
    void sendNextChunk();

private:
    QString generateAuthUrl();
    void sendFirstFrame();
    void sendAudioFrame(const QByteArray &audioData, int status);

    QWebSocket *m_webSocket = nullptr;
    QTimer *m_heartbeatTimer = nullptr;
    QTimer *m_chunkTimer = nullptr;

    QString m_appId;
    QString m_apiKey;
    QString m_apiSecret;
    QString m_host = "iat-api.xfyun.cn";
    QString m_path = "/v2/iat";
    int m_sampleRate = 16000;
    int m_chunkSize = 1280;

    bool m_isConnected = false;
    bool m_isFinishing = false;
    bool m_firstFrameSent = false;
    bool m_completed = false;                    // 防止重复发送完成信号
    QString m_sessionId;

    QQueue<QByteArray> m_pendingChunks;
    QByteArray m_currentSendingChunk;
    int m_currentOffset = 0;

    QString m_partialText;
};

#endif // SPEECHRECOGNISER_H
