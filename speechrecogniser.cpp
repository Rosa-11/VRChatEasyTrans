#include "speechrecogniser.h"
#include "ConfigManager.h"
#include <QUrl>
#include <QUrlQuery>
#include <QDateTime>
#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

SpeechRecogniser::SpeechRecogniser(QObject *parent)
    : QObject(parent)
{
}

SpeechRecogniser::~SpeechRecogniser()
{
    if (m_webSocket) {
        m_webSocket->close();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// initialize()
// ─────────────────────────────────────────────────────────────────────────────
void SpeechRecogniser::initialize()
{
    emit debug("SpeechRecogniser initializing...");

    ConfigManager &cfg = ConfigManager::getInstance();
    m_appId      = cfg.getXunFeiAppId();
    m_apiKey     = cfg.getXunFeiApiKey();
    m_apiSecret  = cfg.getXunFeiApiSecret();
    m_sampleRate = cfg.getSampleRate();

    if (m_appId.isEmpty() || m_apiKey.isEmpty() || m_apiSecret.isEmpty()) {
        emit error("SpeechRecogniser: XunFei credentials not configured");
        return;
    }

    m_webSocket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    connect(m_webSocket, &QWebSocket::connected,
            this, &SpeechRecogniser::onWebSocketConnected);
    connect(m_webSocket, &QWebSocket::textMessageReceived,
            this, &SpeechRecogniser::onTextMessageReceived);
    connect(m_webSocket, &QWebSocket::disconnected,
            this, &SpeechRecogniser::onWebSocketDisconnected);
    connect(m_webSocket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),
            this, &SpeechRecogniser::onWebSocketError);

    m_sendTimer = new QTimer(this);
    m_sendTimer->setSingleShot(true);
    connect(m_sendTimer, &QTimer::timeout,
            this, &SpeechRecogniser::sendNextChunk);

    resetState();

    emit debug(QString("SpeechRecogniser initialized - sampleRate: %1, chunkSize: %2")
                   .arg(m_sampleRate).arg(m_chunkSize));
}

// ─────────────────────────────────────────────────────────────────────────────
// 鉴权
// ─────────────────────────────────────────────────────────────────────────────
QString SpeechRecogniser::formatTimestamp()
{
    return QDateTime::currentDateTimeUtc().toString("ddd, dd MMM yyyy HH:mm:ss 'GMT'");
}

QString SpeechRecogniser::generateAuthUrl()
{
    const QString date = formatTimestamp();
    const QString requestLine     = "GET " + m_path + " HTTP/1.1";
    const QString signatureOrigin = "host: " + m_host + "\ndate: " + date + "\n" + requestLine;

    const QByteArray signatureSha = QMessageAuthenticationCode::hash(
        signatureOrigin.toUtf8(), m_apiSecret.toUtf8(), QCryptographicHash::Sha256);
    const QString signature = signatureSha.toBase64();

    const QString authOrigin = QString(
                                   "api_key=\"%1\", algorithm=\"hmac-sha256\", "
                                   "headers=\"host date request-line\", signature=\"%2\"")
                                   .arg(m_apiKey, signature);
    const QString authorization = QString::fromUtf8(authOrigin.toUtf8().toBase64());

    QUrl url;
    url.setScheme("wss");
    url.setHost(m_host);
    url.setPath(m_path);
    QUrlQuery query;
    query.addQueryItem("host",          m_host);
    query.addQueryItem("date",          date);
    query.addQueryItem("authorization", authorization);
    url.setQuery(query);
    return url.toString();
}

// ─────────────────────────────────────────────────────────────────────────────
// connectToServer()
// ─────────────────────────────────────────────────────────────────────────────
void SpeechRecogniser::connectToServer()
{
    if (m_webSocket->state() == QAbstractSocket::ConnectedState) {
        emit debug("SpeechRecogniser: already connected, skipping");
        return;
    }
    m_webSocket->open(QUrl(generateAuthUrl()));
    emit debug("SpeechRecogniser: connecting to XunFei server...");
}

// ─────────────────────────────────────────────────────────────────────────────
// onStartRecognition()
// ─────────────────────────────────────────────────────────────────────────────
void SpeechRecogniser::onStartRecognition()
{
    if (m_isRecognising && !m_completed) {
        emit debug("SpeechRecogniser: already recognising, ignoring start");
        return;
    }

    resetState();
    emit debug("SpeechRecogniser: start recognition signal received");

    if (m_webSocket && m_webSocket->state() != QAbstractSocket::UnconnectedState) {
        m_isRecognising = true;
        connect(m_webSocket, &QWebSocket::disconnected, this, [this]() {
            disconnect(m_webSocket, &QWebSocket::disconnected, this, nullptr);
            connect(m_webSocket, &QWebSocket::disconnected,
                    this, &SpeechRecogniser::onWebSocketDisconnected);
            connectToServer();
        }, Qt::SingleShotConnection);
        disconnect(m_webSocket, &QWebSocket::disconnected,
                   this, &SpeechRecogniser::onWebSocketDisconnected);
        m_webSocket->close();
    } else {
        m_isRecognising = true;
        connectToServer();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// onSendAudioChunk()
// ─────────────────────────────────────────────────────────────────────────────
void SpeechRecogniser::onSendAudioChunk(const QByteArray &chunk)
{
    if (!m_isRecognising || m_completed) {
        emit debug("SpeechRecogniser: not in recognition state, ignoring chunk");
        return;
    }
    m_pendingChunks.enqueue(chunk);
    emit debug(QString("SpeechRecogniser: chunk queued, queue size: %1").arg(m_pendingChunks.size()));

    if (m_isConnected && !m_sendTimer->isActive()) {
        m_sendTimer->start(40);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// onStopRecognition()
// ─────────────────────────────────────────────────────────────────────────────
void SpeechRecogniser::onStopRecognition()
{
    if (!m_isRecognising || m_completed) {
        emit debug("SpeechRecogniser: not in recognition state, ignoring stop");
        return;
    }
    m_isFinishing = true;
    emit debug("SpeechRecogniser: stop signal received, will send end frame after queue drains");

    if (m_pendingChunks.isEmpty() && m_currentOffset == 0 && !m_sendTimer->isActive()) {
        sendNextChunk();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// onWebSocketConnected()
// ─────────────────────────────────────────────────────────────────────────────
void SpeechRecogniser::onWebSocketConnected()
{
    emit debug("SpeechRecogniser: WebSocket connected");
    m_isConnected = true;

    if (!m_pendingChunks.isEmpty()) {
        sendFirstFrame(m_pendingChunks.dequeue());
        if (!m_pendingChunks.isEmpty()) {
            m_sendTimer->start(40);
        }
    } else {
        emit debug("SpeechRecogniser: connected but no chunks yet, waiting...");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// sendFirstFrame() — 首帧（status=0），携带 common + business + data
// ─────────────────────────────────────────────────────────────────────────────
void SpeechRecogniser::sendFirstFrame(const QByteArray &firstChunk)
{
    QJsonObject frame = {
        {"common",   QJsonObject{{"app_id", m_appId}}},
        {"business", QJsonObject{
                         {"language", "zh_cn"},
                         {"domain",   "iat"},
                         {"accent",   "mandarin"},
                         {"eos",      10000}
                     }},
        {"data", QJsonObject{
                     {"status",   0},
                     {"format",   QString("audio/L16;rate=%1").arg(m_sampleRate)},
                     {"encoding", "raw"},
                     {"audio",    QString::fromUtf8(firstChunk.toBase64())}
                 }}
    };
    m_webSocket->sendTextMessage(QJsonDocument(frame).toJson(QJsonDocument::Compact));
    emit debug(QString("SpeechRecogniser: first frame sent (status=0), audio size: %1 bytes")
                   .arg(firstChunk.size()));
}

// ─────────────────────────────────────────────────────────────────────────────
// sendAudioFrame() — 中间帧（status=1），只携带 data
// ─────────────────────────────────────────────────────────────────────────────
void SpeechRecogniser::sendAudioFrame(const QByteArray &audioData, int status)
{
    QJsonObject frame = {
        {"data", QJsonObject{
                     {"status",   status},
                     {"format",   QString("audio/L16;rate=%1").arg(m_sampleRate)},
                     {"encoding", "raw"},
                     {"audio",    QString::fromUtf8(audioData.toBase64())}
                 }}
    };
    m_webSocket->sendTextMessage(QJsonDocument(frame).toJson(QJsonDocument::Compact));
    emit debug(QString("SpeechRecogniser: audio frame sent (status=%1), size: %2 bytes")
                   .arg(status).arg(audioData.size()));
}

// ─────────────────────────────────────────────────────────────────────────────
// sendEndFrame() — 尾帧（status=2）
//
// ★ 这是本次修复的核心 ★
//
// 讯飞 API 文档对尾帧的描述（原文示例）：
//   { "data": { "status": 2 } }
//
// 尾帧 只有 data.status=2 这一个字段，
// 不含 format / encoding / audio。
//
// 之前的写法 sendAudioFrame(QByteArray(), 2) 会发出：
//   { "data": { "status":2, "format":"audio/L16;rate=16000",
//               "encoding":"raw", "audio":"" } }
//
// audio 是空字符串，base64 解码后是零字节数据，
// 讯飞服务端判定为"无效数据"或"base64解码失败"（错误码 10161），
// 导致整个会话的识别结果为空字符串——这正是您观察到的现象：
// 额度减少、连接正常，但返回内容始终是空。
// ─────────────────────────────────────────────────────────────────────────────
void SpeechRecogniser::sendEndFrame()
{
    // 严格按照文档示例，只包含 data.status=2
    const QJsonObject frame = {
        {"data", QJsonObject{
                     {"status", 2}
                 }}
    };
    m_webSocket->sendTextMessage(QJsonDocument(frame).toJson(QJsonDocument::Compact));
    emit debug("SpeechRecogniser: end frame sent → {\"data\":{\"status\":2}}");
}

// ─────────────────────────────────────────────────────────────────────────────
// sendNextChunk() — 定时器回调
// ─────────────────────────────────────────────────────────────────────────────
void SpeechRecogniser::sendNextChunk()
{
    if (!m_isConnected || m_completed || !m_isRecognising) return;

    // 当前分片还有剩余
    if (m_currentOffset > 0) {
        const int remaining = m_currentChunk.size() - m_currentOffset;
        const int sendSize  = qMin(m_chunkSize, remaining);
        sendAudioFrame(m_currentChunk.mid(m_currentOffset, sendSize), 1);
        m_currentOffset += sendSize;

        if (m_currentOffset < m_currentChunk.size()) {
            m_sendTimer->start(40);
            return;
        } else {
            m_currentChunk.clear();
            m_currentOffset = 0;
        }
    }

    // 取下一个分片
    if (!m_pendingChunks.isEmpty()) {
        m_currentChunk  = m_pendingChunks.dequeue();
        m_currentOffset = 0;
        emit debug(QString("SpeechRecogniser: processing next chunk, queue remaining: %1")
                       .arg(m_pendingChunks.size()));
        m_sendTimer->start(40);
        return;
    }

    // 队列已空，发送尾帧
    if (m_isFinishing && !m_completed) {
        sendEndFrame();   // ← 使用专用尾帧函数，而非 sendAudioFrame(QByteArray(), 2)
        m_isFinishing = false;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// onTextMessageReceived()
// ─────────────────────────────────────────────────────────────────────────────
void SpeechRecogniser::onTextMessageReceived(const QString &message)
{
    if (m_completed) return;

    emit debug(QString("SpeechRecogniser: received: %1").arg(message));

    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (doc.isNull()) {
        emit error("SpeechRecogniser: failed to parse JSON response");
        return;
    }

    const QJsonObject obj = doc.object();

    if (obj.contains("code") && obj["code"].toInt() != 0) {
        emit error(QString("SpeechRecogniser: XunFei API error [%1]: %2")
                       .arg(obj["code"].toInt())
                       .arg(obj["message"].toString()));
        if (m_webSocket) m_webSocket->close();
        return;
    }

    const QJsonObject data   = obj["data"].toObject();
    const int         status = data["status"].toInt();
    const QJsonArray  ws     = data["result"].toObject()["ws"].toArray();

    QString text;
    for (const QJsonValue &wordVal : ws) {
        for (const QJsonValue &cVal : wordVal.toObject()["cw"].toArray()) {
            const QString w = cVal.toObject()["w"].toString();
            if (!w.isEmpty()) text += w;
        }
    }

    if (status == 2) {
        const QString finalText = (m_partialText + text).trimmed();
        if (!finalText.isEmpty()) {
            emit recognitionCompleted(finalText);
            emit debug(QString("SpeechRecogniser: final result: %1").arg(finalText));
        } else {
            emit debug("SpeechRecogniser: recognition completed, no speech detected");
        }
        // 关闭连接，由 onWebSocketDisconnected 统一调用 resetState()
        m_completed = true;
        if (m_webSocket) m_webSocket->close();

    } else if (status == 1 && !text.isEmpty()) {
        m_partialText += text;
        emit debug(QString("SpeechRecogniser: partial result: %1").arg(m_partialText));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// onWebSocketDisconnected() — 唯一调用 resetState() 的地方
// ─────────────────────────────────────────────────────────────────────────────
void SpeechRecogniser::onWebSocketDisconnected()
{
    emit debug("SpeechRecogniser: WebSocket disconnected");
    const bool wasCompleted   = m_completed;
    const bool wasRecognising = m_isRecognising;
    resetState();
    if (!wasCompleted && wasRecognising) {
        emit error("SpeechRecogniser: connection lost before recognition completed");
    }
    emit debug("SpeechRecogniser: ready for next recognition");
}

// ─────────────────────────────────────────────────────────────────────────────
// onWebSocketError()
// ─────────────────────────────────────────────────────────────────────────────
void SpeechRecogniser::onWebSocketError(QAbstractSocket::SocketError socketError)
{
    QString errorMsg;
    switch (socketError) {
    case QAbstractSocket::ConnectionRefusedError: errorMsg = "connection refused"; break;
    case QAbstractSocket::RemoteHostClosedError:  errorMsg = "remote host closed"; break;
    case QAbstractSocket::HostNotFoundError:      errorMsg = "host not found"; break;
    case QAbstractSocket::SocketTimeoutError:     errorMsg = "socket timeout"; break;
    default:                                      errorMsg = m_webSocket->errorString(); break;
    }
    emit error(QString("SpeechRecogniser: WebSocket error: %1").arg(errorMsg));
    m_completed     = true;
    m_isRecognising = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// resetState()
// ─────────────────────────────────────────────────────────────────────────────
void SpeechRecogniser::resetState()
{
    m_isConnected   = false;
    m_isRecognising = false;
    m_isFinishing   = false;
    m_completed     = false;
    m_partialText.clear();
    m_pendingChunks.clear();
    m_currentChunk.clear();
    m_currentOffset = 0;
    if (m_sendTimer && m_sendTimer->isActive()) {
        m_sendTimer->stop();
    }
}
