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
    // 初始化：从 ConfigManager 加载讯飞凭证，创建 WebSocket 和定时器
    void initialize();

    // 收到 AudioCapture::startRecognition 信号：重置状态，建立 WebSocket 连接
    void onStartRecognition();

    // 收到 AudioCapture::sendAudioChunk 信号：将分片放入发送队列
    void onSendAudioChunk(const QByteArray &chunk);

    // 收到 AudioCapture::stopRecognition 信号：标记结束，队列排空后发送尾帧
    void onStopRecognition();

private slots:
    void onWebSocketConnected();
    void onTextMessageReceived(const QString &message);
    void onWebSocketDisconnected();
    void onWebSocketError(QAbstractSocket::SocketError error);

    // 定时器回调：按 40ms 间隔逐片发送队列中的音频数据
    void sendNextChunk();

private:
    // 发起 WebSocket 连接（使用 generateAuthUrl 生成的带鉴权 URL）
    void connectToServer();

    // 生成带 HMAC-SHA256 签名的 WebSocket 鉴权 URL
    QString generateAuthUrl();

    // 【修复】发送首帧：status=0，必须携带第一个音频分片（不能为空）
    void sendFirstFrame(const QByteArray &firstChunk);

    // 发送中间帧（status=1）或尾帧（status=2）
    void sendAudioFrame(const QByteArray &audioData, int status);

    // 重置所有运行时状态（只应由 onWebSocketDisconnected 调用）
    void resetState();

    // 生成 RFC1123 格式的 UTC 时间戳
    QString formatTimestamp();

private:
    // ─── WebSocket ──────────────────────────────────────────
    QWebSocket *m_webSocket = nullptr;
    QTimer     *m_sendTimer = nullptr;  // 速率控制定时器（单次触发，40ms 间隔）

    // ─── 讯飞配置 ───────────────────────────────────────────
    QString m_appId;
    QString m_apiKey;
    QString m_apiSecret;
    QString m_host       = "iat-api.xfyun.cn";
    QString m_path       = "/v2/iat";
    int     m_sampleRate = 16000;
    int     m_chunkSize  = 1280;   // 每次发送 1280 字节（40ms @ 16kHz 16bit）

    // ─── 连接与识别状态 ─────────────────────────────────────
    bool m_isConnected  = false;  // WebSocket 是否已建立连接
    bool m_isRecognising = false; // 是否正在识别中（已发送首帧）
    bool m_isFinishing  = false;  // 是否正在等待发送尾帧
    bool m_completed    = false;  // 是否已收到最终识别结果

    // ─── 分片发送队列 ───────────────────────────────────────
    QQueue<QByteArray> m_pendingChunks;  // 待发送的分片队列
    QByteArray         m_currentChunk;   // 当前正在发送的分片（可能被拆成多个 chunkSize 小块）
    int                m_currentOffset = 0; // 当前分片已发送的字节偏移

    // ─── 识别结果缓存 ───────────────────────────────────────
    QString m_partialText;  // 中间结果累积（status=1 时追加）

signals:
    // 最终识别文本（只在收到 status=2 的完整结果后发出）
    void recognitionCompleted(const QString &text);
    void error(const QString &message);
    void debug(const QString &message);
};

#endif // SPEECHRECOGNISER_H
