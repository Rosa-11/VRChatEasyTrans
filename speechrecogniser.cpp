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

// ─────────────────────────────────────────────────────────────────────────────
// 构造 / 析构
// ─────────────────────────────────────────────────────────────────────────────

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
// initialize() — 从 ConfigManager 加载讯飞凭证，创建 WebSocket 和定时器
// 由主窗口 __start__ 信号触发（在 recogniserThread 中执行）
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

    // 创建 WebSocket（parent=this，随对象析构自动清理）
    m_webSocket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    connect(m_webSocket, &QWebSocket::connected,
            this, &SpeechRecogniser::onWebSocketConnected);
    connect(m_webSocket, &QWebSocket::textMessageReceived,
            this, &SpeechRecogniser::onTextMessageReceived);
    connect(m_webSocket, &QWebSocket::disconnected,
            this, &SpeechRecogniser::onWebSocketDisconnected);
    connect(m_webSocket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),
            this, &SpeechRecogniser::onWebSocketError);

    // 创建发送速率控制定时器（单次触发，每次发完一片后再起下一次）
    m_sendTimer = new QTimer(this);
    m_sendTimer->setSingleShot(true);
    connect(m_sendTimer, &QTimer::timeout,
            this, &SpeechRecogniser::sendNextChunk);

    resetState();

    emit debug(QString("SpeechRecogniser initialized - sampleRate: %1, chunkSize: %2")
                   .arg(m_sampleRate).arg(m_chunkSize));
}

// ─────────────────────────────────────────────────────────────────────────────
// 鉴权相关
// ─────────────────────────────────────────────────────────────────────────────

// 生成 RFC1123 格式的 UTC 时间戳（讯飞 API 要求）
QString SpeechRecogniser::formatTimestamp()
{
    return QDateTime::currentDateTimeUtc().toString("ddd, dd MMM yyyy HH:mm:ss 'GMT'");
}

// 生成带 HMAC-SHA256 签名的 WebSocket 鉴权 URL
// 参考讯飞文档：https://www.xfyun.cn/doc/asr/voicedictation/API.html
QString SpeechRecogniser::generateAuthUrl()
{
    const QString date = formatTimestamp();

    // 1. 拼接签名原始字段（注意换行符和格式，与文档完全一致）
    const QString requestLine      = "GET " + m_path + " HTTP/1.1";
    const QString signatureOrigin  = "host: " + m_host + "\ndate: " + date + "\n" + requestLine;

    // 2. 用 APISecret 对签名原始字段做 HMAC-SHA256，再 Base64 编码
    const QByteArray signatureSha  = QMessageAuthenticationCode::hash(
        signatureOrigin.toUtf8(),
        m_apiSecret.toUtf8(),
        QCryptographicHash::Sha256);
    const QString signature        = signatureSha.toBase64();

    // 3. 拼接 authorization_origin 字符串
    const QString authOrigin = QString(
                                   "api_key=\"%1\", algorithm=\"hmac-sha256\", "
                                   "headers=\"host date request-line\", signature=\"%2\"")
                                   .arg(m_apiKey, signature);

    // 4. 对 authorization_origin 做 Base64 编码，得到最终 authorization 参数
    const QString authorization = QString::fromUtf8(authOrigin.toUtf8().toBase64());

    // 5. 拼接完整 URL
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
// connectToServer() — 发起 WebSocket 连接
// ─────────────────────────────────────────────────────────────────────────────
void SpeechRecogniser::connectToServer()
{
    if (m_webSocket->state() == QAbstractSocket::ConnectedState) {
        emit debug("SpeechRecogniser: already connected, skipping");
        return;
    }
    const QString url = generateAuthUrl();
    m_webSocket->open(QUrl(url));
    emit debug("SpeechRecogniser: connecting to XunFei server...");
}

// ─────────────────────────────────────────────────────────────────────────────
// onStartRecognition() — 收到 AudioCapture 的开始信号
// ─────────────────────────────────────────────────────────────────────────────
void SpeechRecogniser::onStartRecognition()
{
    // 如果上一次识别还没结束，忽略本次开始信号
    if (m_isRecognising && !m_completed) {
        emit debug("SpeechRecogniser: already recognising, ignoring start");
        return;
    }

    // 重置状态，准备新一轮识别
    resetState();
    emit debug("SpeechRecogniser: start recognition signal received");

    if (m_webSocket && m_webSocket->state() != QAbstractSocket::UnconnectedState) {
        // 旧连接还在（比如上次识别刚结束），先关闭，等 disconnected 信号后再连接
        emit debug("SpeechRecogniser: closing old connection before reconnecting...");

        // 【修复】先标记 isRecognising，再发起连接，避免 connected 回调时状态不对
        m_isRecognising = true;

        // 用一次性连接：等旧连接断开后自动发起新连接
        connect(m_webSocket, &QWebSocket::disconnected, this, [this]() {
            // 只响应一次（断开的是旧连接），立刻断开这个临时连接
            disconnect(m_webSocket, &QWebSocket::disconnected, this, nullptr);
            // 重新连接 onWebSocketDisconnected（之前被断开了）
            connect(m_webSocket, &QWebSocket::disconnected,
                    this, &SpeechRecogniser::onWebSocketDisconnected);
            connectToServer();
        }, Qt::SingleShotConnection);

        // 断开永久的 disconnected 连接，避免 onWebSocketDisconnected 在旧连接断开时误报错
        disconnect(m_webSocket, &QWebSocket::disconnected,
                   this, &SpeechRecogniser::onWebSocketDisconnected);
        m_webSocket->close();
    } else {
        // 【修复顺序】先标记正在识别，再发起连接
        // 原来是先 connectToServer() 后再赋值 m_isRecognising，
        // 若握手极快（本地测试），onWebSocketConnected 里检查 m_isRecognising 时可能还是 false
        m_isRecognising = true;
        connectToServer();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// onSendAudioChunk() — 收到 AudioCapture 的音频分片
// 放入队列；若定时器没在跑则启动发送
// ─────────────────────────────────────────────────────────────────────────────
void SpeechRecogniser::onSendAudioChunk(const QByteArray &chunk)
{
    if (!m_isRecognising || m_completed) {
        emit debug("SpeechRecogniser: not in recognition state, ignoring chunk");
        return;
    }

    // 分片入队（无论是否已连接，先缓存；连接成功后统一发送）
    m_pendingChunks.enqueue(chunk);
    emit debug(QString("SpeechRecogniser: chunk queued, queue size: %1").arg(m_pendingChunks.size()));

    // 如果已连接且定时器没在跑，立刻启动发送
    if (m_isConnected && !m_sendTimer->isActive()) {
        m_sendTimer->start(40);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// onStopRecognition() — 收到 AudioCapture 的停止信号
// 标记结束状态，等队列发完后发送尾帧
// ─────────────────────────────────────────────────────────────────────────────
void SpeechRecogniser::onStopRecognition()
{
    if (!m_isRecognising || m_completed) {
        emit debug("SpeechRecogniser: not in recognition state, ignoring stop");
        return;
    }

    m_isFinishing = true;
    emit debug("SpeechRecogniser: stop signal received, will send end frame after queue drains");

    // 【修复】如果队列已空且定时器已停止，主动触发一次 sendNextChunk。
    // 否则当"停止信号到达时队列刚好为空"，定时器不会再触发，尾帧永远发不出去。
    if (m_pendingChunks.isEmpty() && m_currentOffset == 0 && !m_sendTimer->isActive()) {
        sendNextChunk();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// onWebSocketConnected() — WebSocket 握手成功回调
// ─────────────────────────────────────────────────────────────────────────────
void SpeechRecogniser::onWebSocketConnected()
{
    emit debug("SpeechRecogniser: WebSocket connected");
    m_isConnected = true;

    // 【修复】发送首帧时必须携带真实音频数据（讯飞 API 要求 status=0 不能为空）。
    // 从队列中取出第一个分片作为首帧的 audio 字段。
    // 若队列此时还是空的（极端情况：连接比音频快），等后续 onSendAudioChunk 入队时再启动定时器。
    if (!m_pendingChunks.isEmpty()) {
        sendFirstFrame(m_pendingChunks.dequeue());
        // 首帧发完后，继续用定时器发剩余分片
        if (!m_pendingChunks.isEmpty()) {
            m_sendTimer->start(40);
        }
    } else {
        emit debug("SpeechRecogniser: connected but no chunks yet, waiting...");
        // 当 onSendAudioChunk 入队时，它会检测到 m_isConnected==true 并启动定时器
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// sendFirstFrame() — 发送首帧（status=0）
// 讯飞要求首帧必须包含 common / business / data 三个字段，且 audio 不能为空
// ─────────────────────────────────────────────────────────────────────────────
void SpeechRecogniser::sendFirstFrame(const QByteArray &firstChunk)
{
    QJsonObject frame = {
        // common：应用标识
        {"common", QJsonObject{
                       {"app_id", m_appId}
                   }},
        // business：识别业务参数
        {"business", QJsonObject{
                         {"language", "zh_cn"},   // 识别语言（中文普通话）
                         {"domain",   "iat"},     // 应用领域（语音听写）
                         {"accent",   "mandarin"},// 方言（普通话）
                         {"eos",      10000}      // 服务端静音超时（毫秒），兜底防止会话一直挂着
                     }},
        // data：首帧音频数据（status=0 表示第一帧）
        {"data", QJsonObject{
                     {"status",   0},
                     {"format",   QString("audio/L16;rate=%1").arg(m_sampleRate)},
                     {"encoding", "raw"},
                     // 【修复】首帧必须携带真实音频，不能为空字符串
                     {"audio",    QString::fromUtf8(firstChunk.toBase64())}
                 }}
    };

    m_webSocket->sendTextMessage(QJsonDocument(frame).toJson(QJsonDocument::Compact));
    emit debug(QString("SpeechRecogniser: first frame sent (status=0), size: %1 bytes")
                   .arg(firstChunk.size()));
}

// ─────────────────────────────────────────────────────────────────────────────
// sendAudioFrame() — 发送中间帧（status=1）或尾帧（status=2）
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

    if (status == 1) {
        emit debug(QString("SpeechRecogniser: audio frame sent (status=1), size: %1 bytes")
                       .arg(audioData.size()));
    } else if (status == 2) {
        emit debug("SpeechRecogniser: end frame sent (status=2)");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// sendNextChunk() — 定时器回调，从队列取分片并发送（速率控制：每 40ms 一片）
// ─────────────────────────────────────────────────────────────────────────────
void SpeechRecogniser::sendNextChunk()
{
    if (!m_isConnected || m_completed || !m_isRecognising) return;

    // 如果当前分片还有剩余（上次没发完），继续发剩余部分
    if (m_currentOffset > 0) {
        const int remaining = m_currentChunk.size() - m_currentOffset;
        const int sendSize  = qMin(m_chunkSize, remaining);
        const QByteArray slice = m_currentChunk.mid(m_currentOffset, sendSize);
        m_currentOffset += sendSize;
        sendAudioFrame(slice, 1);

        if (m_currentOffset < m_currentChunk.size()) {
            // 当前分片还有更多，40ms 后继续
            m_sendTimer->start(40);
            return;
        } else {
            // 当前分片发完，清空
            m_currentChunk.clear();
            m_currentOffset = 0;
        }
    }

    // 从队列取下一个分片
    if (!m_pendingChunks.isEmpty()) {
        m_currentChunk  = m_pendingChunks.dequeue();
        m_currentOffset = 0;
        emit debug(QString("SpeechRecogniser: processing next chunk, queue remaining: %1")
                       .arg(m_pendingChunks.size()));
        // 40ms 后发第一片
        m_sendTimer->start(40);
        return;
    }

    // 队列已空：检查是否需要发送尾帧
    if (m_isFinishing && !m_completed) {
        // 尾帧 audio 字段为空（讯飞协议允许 status=2 时 audio 为空）
        sendAudioFrame(QByteArray(), 2);
        m_isFinishing = false;
        // 等待服务端返回最终结果（onTextMessageReceived 里处理 status==2 的响应）
    }
    // 注意：这里不启动定时器，等待下一个 onSendAudioChunk 或 onStopRecognition 触发
}

// ─────────────────────────────────────────────────────────────────────────────
// onTextMessageReceived() — 处理讯飞 API 返回的 JSON 消息
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

    // 检查 API 返回的错误码（code != 0 表示出错）
    if (obj.contains("code") && obj["code"].toInt() != 0) {
        const int     code = obj["code"].toInt();
        const QString msg  = obj["message"].toString();
        emit error(QString("SpeechRecogniser: XunFei API error [%1]: %2").arg(code).arg(msg));
        // 出错时关闭连接，由 onWebSocketDisconnected 做最终的 resetState
        if (m_webSocket) m_webSocket->close();
        return;
    }

    // 提取识别文本
    const QJsonObject data   = obj["data"].toObject();
    const int         status = data["status"].toInt();
    const QJsonObject result = data["result"].toObject();
    const QJsonArray  ws     = result["ws"].toArray();

    QString text;
    for (const QJsonValue &wordVal : ws) {
        const QJsonArray cw = wordVal.toObject()["cw"].toArray();
        for (const QJsonValue &cVal : cw) {
            const QString word = cVal.toObject()["w"].toString();
            if (!word.isEmpty()) text += word;
        }
    }

    if (status == 2) {
        // 收到最终结果帧（服务端已完成识别）
        const QString finalText = (m_partialText + text).trimmed();

        if (!finalText.isEmpty()) {
            emit recognitionCompleted(finalText);
            emit debug(QString("SpeechRecogniser: final result: %1").arg(finalText));
        } else {
            emit debug("SpeechRecogniser: recognition completed, no speech detected");
        }

        // 【修复】不在这里调用 resetState()，而是让 WebSocket 关闭后
        // 由 onWebSocketDisconnected 统一做清理，避免竞态。
        // 只设置 completed 标志，阻止后续分片继续发送。
        m_completed = true;

        // 关闭连接，触发 onWebSocketDisconnected
        if (m_webSocket) m_webSocket->close();

    } else if (status == 1 && !text.isEmpty()) {
        // 中间结果（实时返回，追加到 partialText）
        m_partialText += text;
        emit debug(QString("SpeechRecogniser: partial result: %1").arg(m_partialText));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// onWebSocketDisconnected() — WebSocket 断开回调
// 【修复】这里是唯一调用 resetState() 的地方，避免竞态
// ─────────────────────────────────────────────────────────────────────────────
void SpeechRecogniser::onWebSocketDisconnected()
{
    emit debug("SpeechRecogniser: WebSocket disconnected");

    // 判断本次断开是正常结束还是异常断开
    const bool wasCompleted    = m_completed;
    const bool wasRecognising  = m_isRecognising;

    // 统一在此重置所有状态（不在其他地方分散调用）
    resetState();

    if (!wasCompleted && wasRecognising) {
        // 识别进行中连接意外断开
        emit error("SpeechRecogniser: connection lost before recognition completed");
    }

    emit debug("SpeechRecogniser: ready for next recognition");
}

// ─────────────────────────────────────────────────────────────────────────────
// onWebSocketError() — WebSocket 错误回调
// ─────────────────────────────────────────────────────────────────────────────
void SpeechRecogniser::onWebSocketError(QAbstractSocket::SocketError socketError)
{
    QString errorMsg;
    switch (socketError) {
    case QAbstractSocket::ConnectionRefusedError:
        errorMsg = "connection refused";
        break;
    case QAbstractSocket::RemoteHostClosedError:
        errorMsg = "remote host closed connection";
        break;
    case QAbstractSocket::HostNotFoundError:
        errorMsg = "host not found";
        break;
    case QAbstractSocket::SocketTimeoutError:
        errorMsg = "socket timeout";
        break;
    default:
        errorMsg = m_webSocket->errorString();
        break;
    }
    emit error(QString("SpeechRecogniser: WebSocket error: %1").arg(errorMsg));

    // 标记完成，防止 onWebSocketDisconnected 误报"识别中断开"
    m_completed     = true;
    m_isRecognising = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// resetState() — 将所有运行时状态重置为初始值
// 只应由 onWebSocketDisconnected 调用，其余地方不要直接调用
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
