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
    void onStartRecognition();
    void onSendAudioChunk(const QByteArray &chunk);
    void onStopRecognition();

private slots:
    void onWebSocketConnected();
    void onTextMessageReceived(const QString &message);
    void onWebSocketDisconnected();
    void onWebSocketError(QAbstractSocket::SocketError error);

private:
    void connectToServer();
    QString generateAuthUrl();
    QString formatTimestamp();

    // 一次性发送所有音频（非分片方式）
    void sendFullAudio();

    void resetState();

private:
    QWebSocket *m_webSocket = nullptr;
    QTimer     *m_connectTimer = nullptr;  // 连接超时定时器

    QString m_appId;
    QString m_apiKey;
    QString m_apiSecret;
    QString m_host       = "iat-api.xfyun.cn";
    QString m_path       = "/v2/iat";
    int     m_sampleRate = 16000;

    // 状态标志
    bool m_isConnected   = false;
    bool m_isRecognising = false;
    bool m_isCollecting  = false;   // 是否正在收集音频
    bool m_completed     = false;

    // 收集的完整音频数据（PCM格式，16kHz/16bit/单声道）
    QByteArray m_accumulatedAudio;

    // 识别结果
    QString m_partialText;
    QString m_finalText;

signals:
    void recognitionCompleted(const QString &text);
    void error(const QString &message);
    void debug(const QString &message);
};

#endif // SPEECHRECOGNISER_H
