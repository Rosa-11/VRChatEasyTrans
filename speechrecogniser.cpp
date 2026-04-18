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
#include <QUuid>
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

void SpeechRecogniser::initialize()
{
    qDebug() << "\nSpeechRecogniser initialize";

    ConfigManager &cfg = ConfigManager::getInstance();
    m_appId = cfg.getXunFeiAppId();
    m_apiKey = cfg.getXunFeiApiKey();
    m_apiSecret = cfg.getXunFeiApiSecret();
    m_sampleRate = cfg.getSampleRate();

    m_webSocket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    connect(m_webSocket, &QWebSocket::connected, this, &SpeechRecogniser::onConnected);
    connect(m_webSocket, &QWebSocket::textMessageReceived, this, &SpeechRecogniser::onTextMessageReceived);
    connect(m_webSocket, &QWebSocket::disconnected, this, &SpeechRecogniser::onDisconnected);
    connect(m_webSocket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),
            this, &SpeechRecogniser::onError);

    m_chunkTimer = new QTimer(this);
    m_chunkTimer->setSingleShot(true);
    connect(m_chunkTimer, &QTimer::timeout, this, &SpeechRecogniser::sendNextChunk);

    m_sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_completed = false;
}

void SpeechRecogniser::processAudioData(const QByteArray &data)
{
    if (m_completed) return;

    if (!m_isConnected) {
        m_pendingChunks.enqueue(data);
        if (m_webSocket->state() == QAbstractSocket::UnconnectedState) {
            QString url = generateAuthUrl();
            m_webSocket->open(QUrl(url));
            emit socketStateChanged("Connecting...");
        }
        return;
    }

    m_pendingChunks.enqueue(data);
    if (!m_chunkTimer->isActive()) {
        m_chunkTimer->start(0);
    }
}

void SpeechRecogniser::finishRecognition()
{
    qDebug() << "SpeechRecogniser: finishing recognition";
    if (m_completed) return;

    m_isFinishing = true;

    if (!m_isConnected) {
        // 从未连上服务器，视为错误，不触发翻译
        emit error("Recognition failed: could not connect to server.");
        m_completed = true;
        return;
    }

    // 等待发送队列处理完毕，由 sendNextChunk 发送结束帧
    if (!m_chunkTimer->isActive() && m_pendingChunks.isEmpty() && m_currentOffset == 0) {
        sendAudioFrame(QByteArray(), 2);
    }
}

QString SpeechRecogniser::generateAuthUrl()
{
    QDateTime now = QDateTime::currentDateTimeUtc();
    QString date = now.toString("ddd, dd MMM yyyy HH:mm:ss 'GMT'");

    QString requestLine = "GET " + m_path + " HTTP/1.1";
    QString signatureOrigin = "host: " + m_host + "\ndate: " + date + "\n" + requestLine;

    QByteArray signatureSha = QMessageAuthenticationCode::hash(
        signatureOrigin.toUtf8(),
        m_apiSecret.toUtf8(),
        QCryptographicHash::Sha256);
    QString signature = signatureSha.toBase64();

    QString authOrigin = QString("api_key=\"%1\", algorithm=\"hmac-sha256\", "
                                 "headers=\"host date request-line\", signature=\"%2\"")
                             .arg(m_apiKey, signature);
    QString authorization = authOrigin.toUtf8().toBase64();

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

void SpeechRecogniser::onConnected()
{
    qDebug() << "WebSocket connected.";
    m_isConnected = true;
    emit socketStateChanged("Connected");

    sendFirstFrame();

    if (!m_pendingChunks.isEmpty() && !m_chunkTimer->isActive()) {
        m_chunkTimer->start(0);
    }
}

void SpeechRecogniser::sendFirstFrame()
{
    QJsonObject frame = {
        {
            "common", QJsonObject
            {
               {"app_id", m_appId}
            }
        },
        {
            "business", QJsonObject
            {
                {"language", "zh_cn"},
                {"domain", "iat"},
                {"accent", "mandarin"}
            }
        },
        {"data", QJsonObject
            {
                 {"status", 0},
                 {"format", QString("audio/L16;rate=%1").arg(m_sampleRate)},
                 {"encoding", "raw"},
                 {"audio", ""}
            }
        }
    };

    m_webSocket->sendTextMessage(QJsonDocument(frame).toJson(QJsonDocument::Compact));
    m_firstFrameSent = true;
}

void SpeechRecogniser::sendAudioFrame(const QByteArray &audioData, int status)
{
    QJsonObject frame = {
        {"data", QJsonObject
            {
                {"status", status},
                {"format", QString("audio/L16;rate=%1").arg(m_sampleRate)},
                {"encoding", "raw"},
                {"audio", QString::fromUtf8(audioData.toBase64())}
            }
        }
    };
    m_webSocket->sendTextMessage(QJsonDocument(frame).toJson(QJsonDocument::Compact));
}

void SpeechRecogniser::sendNextChunk()
{
    if (!m_isConnected || m_completed) return;

    // 继续发送当前分块的剩余部分
    if (m_currentOffset > 0) {
        int remaining = m_currentSendingChunk.size() - m_currentOffset;
        int chunkSize = qMin(m_chunkSize, remaining);
        QByteArray chunk = m_currentSendingChunk.mid(m_currentOffset, chunkSize);
        m_currentOffset += chunkSize;
        sendAudioFrame(chunk, 1);

        if (m_currentOffset < m_currentSendingChunk.size()) {
            m_chunkTimer->start(40);
            return;
        } else {
            m_currentOffset = 0;
            m_currentSendingChunk.clear();
        }
    }

    // 处理队列中的下一块
    if (!m_pendingChunks.isEmpty()) {
        m_currentSendingChunk = m_pendingChunks.dequeue();
        m_currentOffset = 0;
        m_chunkTimer->start(0);
        return;
    }

    // 队列空，若正在结束状态则发送结束帧
    if (m_isFinishing && !m_completed) {
        sendAudioFrame(QByteArray(), 2);
        m_isFinishing = false; // 避免重复发送结束帧
        qDebug() << "Sent end-of-stream frame.";
    }
}

void SpeechRecogniser::onTextMessageReceived(const QString &message)
{
    if (m_completed) return;

    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (doc.isNull()) return;

    QJsonObject obj = doc.object();

    if (obj.contains("code") && obj["code"].toInt() != 0) {
        emit error(QString("API Error: %1 - %2").arg(obj["code"].toInt()).arg(obj["message"].toString()));
        m_webSocket->close();
        m_completed = true;
        return;
    }

    QJsonObject data = obj["data"].toObject();
    int status = data["status"].toInt();

    // 解析识别文本
    QString text;
    QJsonArray ws = data["result"].toObject()["ws"].toArray();
    for (const QJsonValue &wordVal : ws) {
        QJsonArray cw = wordVal.toObject()["cw"].toArray();
        for (const QJsonValue &cVal : cw) {
            text += cVal.toObject()["w"].toString();
        }
    }

    if (!text.isEmpty()) {
        if (status == 1) {
            m_partialText += text;
            emit recognitionPartial(m_partialText);
        } else if (status == 2) {
            m_partialText += text;
            QString finalText = m_partialText.trimmed();

            if (!finalText.isEmpty()) {
                emit recognitionCompleted(finalText);   // ✅ 唯一成功出口
                emit debug("Recognition completed: " + finalText);
            } else {
                emit error("Recognition result is empty.");
            }
            m_completed = true;
            m_webSocket->close(QWebSocketProtocol::CloseCodeNormal, "Finished");
            return;
        }
    }

    if (status == 2) {
        // 最终结果但文本为空
        emit error("Recognition completed with empty text.");
        m_completed = true;
        m_webSocket->close();
    }
}

void SpeechRecogniser::onDisconnected()
{
    m_isConnected = false;
    m_firstFrameSent = false;
    emit socketStateChanged("Disconnected");

    // 非正常完成（如网络断开）且尚未处理完成时，报告错误
    if (!m_completed) {
        emit error("Connection lost before recognition completed.");
        m_completed = true;
    }
}

void SpeechRecogniser::onError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error)
    QString errMsg = m_webSocket->errorString();
    qCritical() << "WebSocket error:" << errMsg;
    emit this->error("WebSocket error: " + errMsg);
    m_completed = true;
}
