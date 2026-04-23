#ifndef SPEECHRECOGNISER_H
#define SPEECHRECOGNISER_H

#include <QObject>
#include <QWebSocket>
#include <QTimer>
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
    void sendNextChunk();

private:
    void connectToServer();
    QString generateAuthUrl();
    QString formatTimestamp();

    // 首帧（status=0）：携带 common + business + data，audio 不能为空
    void sendFirstFrame(const QByteArray &firstChunk);

    // 中间帧（status=1）：只携带 data，含 format/encoding/audio
    void sendAudioFrame(const QByteArray &audioData, int status);

    // ★ 尾帧（status=2）：严格按讯飞文档，只发 {"data":{"status":2}}
    //   不含 format / encoding / audio 字段
    //   这是导致识别结果为空字符串的根本原因
    void sendEndFrame();

    void resetState();

private:
    QWebSocket *m_webSocket = nullptr;
    QTimer     *m_sendTimer = nullptr;

    QString m_appId;
    QString m_apiKey;
    QString m_apiSecret;
    QString m_host       = "iat-api.xfyun.cn";
    QString m_path       = "/v2/iat";
    int     m_sampleRate = 16000;
    int     m_chunkSize  = 1280;

    bool m_isConnected   = false;
    bool m_isRecognising = false;
    bool m_isFinishing   = false;
    bool m_completed     = false;

    QQueue<QByteArray> m_pendingChunks;
    QByteArray         m_currentChunk;
    int                m_currentOffset = 0;

    QString m_partialText;

signals:
    void recognitionCompleted(const QString &text);
    void error(const QString &message);
    void debug(const QString &message);
};

#endif // SPEECHRECOGNISER_H
