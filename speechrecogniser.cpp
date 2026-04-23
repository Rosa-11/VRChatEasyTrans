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
#include <QThread>

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

    m_connectTimer = new QTimer(this);
    m_connectTimer->setSingleShot(true);

    resetState();

    emit debug(QString("SpeechRecogniser initialized - 采样率: %1")
                   .arg(m_sampleRate));
}

// ─────────────────────────────────────────────────────────────────────────────
// formatTimestamp() - RFC1123格式时间戳
// ─────────────────────────────────────────────────────────────────────────────
QString SpeechRecogniser::formatTimestamp()
{
    return QDateTime::currentDateTimeUtc().toString("ddd, dd MMM yyyy hh:mm:ss") + " GMT";
}

// ─────────────────────────────────────────────────────────────────────────────
// generateAuthUrl() - 生成带鉴权的WebSocket URL
// ─────────────────────────────────────────────────────────────────────────────
QString SpeechRecogniser::generateAuthUrl()
{
    const QString date = formatTimestamp();
    const QString requestLine = "GET " + m_path + " HTTP/1.1";
    const QString signatureOrigin = "host: " + m_host + "\ndate: " + date + "\n" + requestLine;

    // HMAC-SHA256签名
    QMessageAuthenticationCode hmac(QCryptographicHash::Sha256);
    hmac.setKey(m_apiSecret.toUtf8());
    hmac.addData(signatureOrigin.toUtf8());
    QByteArray signature = hmac.result().toBase64();

    // Authorization原始字符串
    QString authorizationOrigin = QString("api_key=\"%1\", algorithm=\"hmac-sha256\", "
                                          "headers=\"host date request-line\", signature=\"%2\"")
                                      .arg(m_apiKey, QString::fromUtf8(signature));

    // Base64编码
    QString authorization = QString::fromUtf8(authorizationOrigin.toUtf8().toBase64());

    QUrl url;
    url.setScheme("wss");
    url.setHost(m_host);
    url.setPath(m_path);
    QUrlQuery query;
    query.addQueryItem("host", m_host);
    query.addQueryItem("date", date);
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
        return;
    }

    QString url = generateAuthUrl();
    m_webSocket->open(QUrl(url));

    // 设置连接超时（5秒）
    m_connectTimer->start(5000);
}

// ─────────────────────────────────────────────────────────────────────────────
// onStartRecognition() - 开始收集音频
// ─────────────────────────────────────────────────────────────────────────────
void SpeechRecogniser::onStartRecognition()
{
    if (m_isRecognising || m_isCollecting) {
        return;
    }

    resetState();
    m_isCollecting = true;
    m_accumulatedAudio.clear();
    m_partialText.clear();
    m_finalText.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// onSendAudioChunk() - 收集音频分片
// ─────────────────────────────────────────────────────────────────────────────
void SpeechRecogniser::onSendAudioChunk(const QByteArray &chunk)
{
    if (!m_isCollecting) {
        // 不在收集状态，忽略
        return;
    }
    m_accumulatedAudio.append(chunk);
}

// ─────────────────────────────────────────────────────────────────────────────
// onStopRecognition() - 停止收集，开始识别
// ─────────────────────────────────────────────────────────────────────────────
void SpeechRecogniser::onStopRecognition()
{
    if (!m_isCollecting) {
        return;
    }

    m_isCollecting = false;
    m_isRecognising = true;

    int durationMs = m_accumulatedAudio.size() * 1000 / (m_sampleRate * 2);

    if (m_accumulatedAudio.isEmpty()) {
        resetState();
        return;
    }

    // 检查WebSocket连接状态
    if (m_webSocket->state() == QAbstractSocket::ConnectedState) {
        // 已连接，直接发送音频
        sendFullAudio();
    } else if (m_webSocket->state() == QAbstractSocket::ConnectingState) {
        emit debug("SpeechRecogniser: waiting for connection to complete...");
    } else {
        connectToServer();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// onWebSocketConnected()
// ─────────────────────────────────────────────────────────────────────────────
void SpeechRecogniser::onWebSocketConnected()
{
    m_connectTimer->stop();
    m_isConnected = true;

    // 如果有待发送的音频，立即发送
    if (m_isRecognising && !m_accumulatedAudio.isEmpty()) {
        sendFullAudio();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// sendFullAudio() - 一次性发送完整音频（参考旧版本逻辑）
//
// 关键点：
//   1. 所有音频数据放在第一帧（status=0）的 data.audio 中
//   2. 立即发送尾帧（status=2）结束识别
//   3. 不分片发送，避免讯飞返回空结果
// ─────────────────────────────────────────────────────────────────────────────
void SpeechRecogniser::sendFullAudio()
{
    if (!m_webSocket || m_webSocket->state() != QAbstractSocket::ConnectedState) {
        resetState();
        return;
    }

    if (m_accumulatedAudio.isEmpty()) {
        resetState();
        return;
    }

    // ─── 第一帧（status=0）：携带 common + business + data ───
    QJsonObject common;
    common["app_id"] = m_appId;

    QJsonObject business;
    business["language"] = "zh_cn";
    business["domain"] = "iat";
    business["accent"] = "mandarin";
    business["eos"] = 10000;  // 静音检测时长（毫秒）

    // 音频Base64编码
    QString audioBase64 = QString::fromUtf8(m_accumulatedAudio.toBase64());

    QJsonObject data;
    data["status"] = 0;   // 第一帧（也是唯一的数据帧）
    data["format"] = QString("audio/L16;rate=%1").arg(m_sampleRate);
    data["encoding"] = "raw";
    data["audio"] = audioBase64;

    QJsonObject firstFrame;
    firstFrame["common"] = common;
    firstFrame["business"] = business;
    firstFrame["data"] = data;

    QString firstFrameStr = QString::fromUtf8(
        QJsonDocument(firstFrame).toJson(QJsonDocument::Compact));
    m_webSocket->sendTextMessage(firstFrameStr);

    // ─── 尾帧（status=2）：根据讯飞文档，只包含 data.status=2 ───
    QJsonObject endData;
    endData["status"] = 2;

    QJsonObject endFrame;
    endFrame["data"] = endData;

    QString endFrameStr = QString::fromUtf8(
        QJsonDocument(endFrame).toJson(QJsonDocument::Compact));
    m_webSocket->sendTextMessage(endFrameStr);

    // 注意：不要立即关闭连接，等待识别结果返回后再关闭
    // 识别结果会在 onTextMessageReceived 中处理
}

// ─────────────────────────────────────────────────────────────────────────────
// onTextMessageReceived() - 处理讯飞返回的消息
// ─────────────────────────────────────────────────────────────────────────────
void SpeechRecogniser::onTextMessageReceived(const QString &message)
{
    if (m_completed) return;

    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (doc.isNull()) {
        return;
    }

    const QJsonObject obj = doc.object();

    // 检查错误码
    if (obj.contains("code") && obj["code"].toInt() != 0) {
        int errorCode = obj["code"].toInt();
        QString errorMsg = obj["message"].toString("Unknown error");
        emit error(QString("SpeechRecogniser: XunFei API error [%1]: %2")
                       .arg(errorCode).arg(errorMsg));
        m_completed = true;
        if (m_webSocket) m_webSocket->close();
        return;
    }

    // 解析识别结果
    if (obj.contains("data")) {
        const QJsonObject dataObj = obj["data"].toObject();
        const int status = dataObj["status"].toInt();

        // 解析文本
        if (dataObj.contains("result")) {
            const QJsonObject resultObj = dataObj["result"].toObject();
            if (resultObj.contains("ws")) {
                QString text;
                const QJsonArray wsArray = resultObj["ws"].toArray();
                for (const QJsonValue &wsValue : wsArray) {
                    const QJsonObject wsObj = wsValue.toObject();
                    if (wsObj.contains("cw")) {
                        const QJsonArray cwArray = wsObj["cw"].toArray();
                        for (const QJsonValue &cwValue : cwArray) {
                            const QJsonObject cwObj = cwValue.toObject();
                            if (cwObj.contains("w")) {
                                text += cwObj["w"].toString();
                            }
                        }
                    }
                }
                if (!text.isEmpty()) {
                    m_partialText += text;
                }
            }
        }

        // 最终结果（status=2）
        if (status == 2) {
            m_finalText = m_partialText.trimmed();
            if (!m_finalText.isEmpty()) {
                emit recognitionCompleted(m_finalText);
                emit debug(QString("识别结果: %1").arg(m_finalText));
            } else {
                emit debug("未识别到文本");
            }
            m_completed = true;
            if (m_webSocket) m_webSocket->close();
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// onWebSocketDisconnected()
// ─────────────────────────────────────────────────────────────────────────────
void SpeechRecogniser::onWebSocketDisconnected()
{
    resetState();
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

    if (m_connectTimer->isActive()) {
        m_connectTimer->stop();
    }

    m_completed = true;
    m_isRecognising = false;
    resetState();
}

// ─────────────────────────────────────────────────────────────────────────────
// resetState()
// ─────────────────────────────────────────────────────────────────────────────
void SpeechRecogniser::resetState()
{
    m_isConnected   = false;
    m_isRecognising = false;
    m_isCollecting  = false;
    m_completed     = false;
    m_accumulatedAudio.clear();
    m_partialText.clear();
    m_finalText.clear();

    if (m_connectTimer && m_connectTimer->isActive()) {
        m_connectTimer->stop();
    }
}
