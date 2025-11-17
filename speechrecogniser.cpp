#include "speechrecogniser.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QEventLoop>
#include <QTimer>
#include <QDebug>
#include <cmath>
#include <QWebSocket>
#include <QUrlQuery>
#include <QMessageAuthenticationCode>
#include <QCryptographicHash>

SpeechRecogniser::SpeechRecogniser(QObject* parent)
    : QObject(parent)
    , config(ConfigManager::instance())
{}

SpeechRecogniser::~SpeechRecogniser()
{}

QString SpeechRecogniser::generateAuthorizationHeader()
{
    // 从ConfigManager获取配置
    QString apiKey = config.getXunFeiApiKey();
    QString apiSecret = config.getXunFeiApiSecret();

    if (apiKey.isEmpty() || apiSecret.isEmpty()) {
        qDebug() << "Error: XunFei API configuration is incomplete";
        return "";
    }

    // 生成RFC1123格式的时间戳
    QDateTime currentTime = QDateTime::currentDateTimeUtc();
    QString date = currentTime.toString("ddd, dd MMM yyyy hh:mm:ss") + " GMT";

    // 生成签名原始字符串
    QString host = "iat-api.xfyun.cn";
    QString requestLine = "GET /v2/iat HTTP/1.1";
    QString signatureOrigin = QString("host: %1\ndate: %2\n%3").arg(host).arg(date).arg(requestLine);

    // 使用API Secret进行HMAC-SHA256加密
    QMessageAuthenticationCode hmac(QCryptographicHash::Sha256);
    hmac.setKey(apiSecret.toUtf8());
    hmac.addData(signatureOrigin.toUtf8());
    QByteArray signature = hmac.result().toBase64();

    // 生成authorization原始字符串并进行base64编码
    QString authorizationOrigin = QString("api_key=\"%1\", algorithm=\"%2\", headers=\"%3\", signature=\"%4\"")
                                      .arg(apiKey)
                                      .arg("hmac-sha256")
                                      .arg("host date request-line")
                                      .arg(QString(signature));

    return authorizationOrigin.toUtf8().toBase64();
}

QString SpeechRecogniser::recognizeSpeech(const QVector<float>& audio_data, int sample_rate, int channels)
{
    if (audio_data.isEmpty()) {
        qDebug() << "Audio data is empty";
        return "";
    }

    if (config.getXunFeiAppId().isEmpty() ||
        config.getXunFeiApiKey().isEmpty() ||
        config.getXunFeiApiSecret().isEmpty()) {
        qDebug() << "XunFei API configuration is incomplete";
        return "Error: API configuration incomplete";
    }

    QWebSocket webSocket;
    QString recognitionResult;
    bool recognitionCompleted = false;

    QObject::connect(&webSocket, &QWebSocket::connected, [&]() {
        qDebug() << "WebSocket connected, sending audio data...";
        sendAudioData(&webSocket, audio_data);
    });

    QObject::connect(&webSocket, &QWebSocket::textMessageReceived, [&](const QString& message) {
        QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
        if (doc.isNull()) {
            qDebug() << "Null net package";
            return;
        }

        QJsonObject obj = doc.object();

        if (obj.contains("code") && obj["code"].toInt() != 0) {
            int errorCode = obj["code"].toInt();
            QString errorMsg = obj["message"].toString("Unknown error");
            recognitionResult = QString("Error[%1]: %2").arg(errorCode).arg(errorMsg);
            recognitionCompleted = true;
            return;
        }

        // 解析识别结果
        if (obj.contains("data")) {
            QJsonObject dataObj = obj["data"].toObject();
            int status = dataObj["status"].toInt();

            if (dataObj.contains("result")) {
                QJsonObject resultObj = dataObj["result"].toObject();

                if (resultObj.contains("bg")) {
                    qDebug() << "Begin Time:" << resultObj["bg"].toInt();
                }

                if (resultObj.contains("ed")) {
                    qDebug() << "End Time:" << resultObj["ed"].toInt();
                }

                if (resultObj.contains("ls")) {
                    qDebug() << "Last Segment:" << resultObj["ls"].toBool();
                }

                if (resultObj.contains("sn")) {
                    qDebug() << "Sequence Number:" << resultObj["sn"].toInt();
                }

                // 解析文本
                if (resultObj.contains("ws")) {
                    QString text;
                    QJsonArray wsArray = resultObj["ws"].toArray();
                    for (const QJsonValue& wsValue : wsArray) {
                        QJsonObject wsObj = wsValue.toObject();
                        if (wsObj.contains("cw")) {
                            QJsonArray cwArray = wsObj["cw"].toArray();
                            for (const QJsonValue& cwValue : cwArray) {
                                QJsonObject cwObj = cwValue.toObject();
                                if (cwObj.contains("w")) {
                                    text += cwObj["w"].toString();
                                }
                            }
                        }
                    }
                    if (!text.isEmpty()) {
                        if (!recognitionResult.isEmpty()) {
                            recognitionResult += " ";
                            recognitionResult += text;
                        } else {
                            recognitionResult = text;
                        }
                        qDebug() << "Accumulated recognition text:" << recognitionResult;
                    }
                }
            }
            if (status == 2) {
                qDebug() << "Recognition completed";
                recognitionCompleted = true;
            }
        }
    });

    QObject::connect(&webSocket, &QWebSocket::disconnected, [&]() {
        qDebug() << "WebSocket disconnected";
        recognitionCompleted = true;
    });

    QObject::connect(&webSocket, &QWebSocket::errorOccurred, [&](QAbstractSocket::SocketError error) {
        qDebug() << "WebSocket error:" << webSocket.errorString();
        recognitionResult = "Error: " + webSocket.errorString();
        recognitionCompleted = true;
    });

    // 生成认证并建立连接
    QString authorization = generateAuthorizationHeader();
    if (authorization.isEmpty()) {
        return "Error: Failed to generate authorization";
    }

    QDateTime currentTime = QDateTime::currentDateTimeUtc();
    QString date = currentTime.toString("ddd, dd MMM yyyy hh:mm:ss") + " GMT";

    QUrl url("wss://iat-api.xfyun.cn/v2/iat");
    QUrlQuery query;
    query.addQueryItem("authorization", authorization);
    query.addQueryItem("date", date);
    query.addQueryItem("host", "iat-api.xfyun.cn");
    url.setQuery(query);

    qDebug() << "Connecting to XunFei speech recognition...";
    webSocket.open(url);

    // 等待识别完成
    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);

    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeoutTimer.start(15000); // 15秒超时

    QTimer checkTimer;
    QObject::connect(&checkTimer, &QTimer::timeout, [&]() {
        if (recognitionCompleted) {
            loop.quit();
        }
    });
    checkTimer.start(100);

    loop.exec();

    if (webSocket.state() != QAbstractSocket::UnconnectedState) {
        webSocket.close();
    }

    qDebug() << "Final recognition result:" << recognitionResult;
    return recognitionResult;
}

QVector<int16_t> SpeechRecogniser::convertFloatToInt16(const QVector<float>& audio_data)
{
    QVector<int16_t> int16_data;
    int16_data.resize(audio_data.size());

    for (int i = 0; i < audio_data.size(); ++i) {
        // audio输入在-1.0到1.0范围内，不是我喜欢的类型，直接将float转换为int16
        float sample = qBound(-1.0f, audio_data[i], 1.0f);
        int16_data[i] = static_cast<int16_t>(sample * 32767.0f);
    }

    return int16_data;
}

void SpeechRecogniser::sendAudioData(QWebSocket* webSocket, const QVector<float>& audio_data)
{
    if (!webSocket) return;

    // 转换为16位 PCM
    QVector<int16_t> pcmData = convertFloatToInt16(audio_data);

    QJsonObject common;
    common["app_id"] = config.getXunFeiAppId();

    QJsonObject business;
    business["language"] = "zh_cn";
    business["domain"] = "iat";
    business["accent"] = "mandarin";

    QByteArray audioBytes(reinterpret_cast<const char*>(pcmData.constData()),
                          pcmData.size() * sizeof(int16_t));
    QString audioBase64 = QString(audioBytes.toBase64());

    QJsonObject data;
    data["status"] = 0;  // 第一帧
    data["format"] = "audio/L16;rate=16000";
    data["encoding"] = "raw";
    data["audio"] = audioBase64;

    QJsonObject message;
    message["common"] = common;
    message["business"] = business;
    message["data"] = data;

    QJsonDocument doc(message);
    QString messageStr = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));

    qDebug() << "Sending audio data, size:" << audio_data.size() << "samples";
    webSocket->sendTextMessage(messageStr);

    QJsonObject endData;
    endData["status"] = 2;

    QJsonObject endMessage;
    endMessage["data"] = endData;

    QJsonDocument endDoc(endMessage);
    QString endMsgStr = QString::fromUtf8(endDoc.toJson(QJsonDocument::Compact));
    webSocket->sendTextMessage(endMsgStr);

    qDebug() << "Audio data sent";
}
