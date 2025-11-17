#include "translator.h"
#include "configmanager.h"
#include <QNetworkRequest>
#include <QUrl>
#include <QJsonArray>
#include <QEventLoop>
#include <QTimer>

Translator::Translator(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_asyncMode(false)
    , config(ConfigManager::instance())
{
    connect(m_networkManager, &QNetworkAccessManager::finished,
            this, &Translator::onReplyFinished);
}

QString Translator::translateText(const QString& text, const QString& targetLanguage)
{
    // 获取API Key
    ConfigManager& config = ConfigManager::instance();
    QString apiKey = config.getDeepseekApiKey();

    if (apiKey.isEmpty()) {
        qDebug() << "Deepseek API Key is empty";
        return "Error: API Key not configured";
    }

    // 构建网络请求
    QNetworkRequest request;
    request.setUrl(QUrl("https://api.deepseek.com/v1/chat/completions"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(apiKey).toUtf8());

    // 构建请求JSON
    QString requestJson = buildRequestJson(text, targetLanguage);

    // 同步请求
    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);

    QNetworkReply* reply = m_networkManager->post(request, requestJson.toUtf8());

    // 连接超时和完成信号
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeoutTimer.start(30000); // 30秒超时

    loop.exec();

    if (!timeoutTimer.isActive()) {
        // 超时
        reply->abort();
        return "Error: Translation timeout";
    }

    // 处理响应
    if (reply->error() != QNetworkReply::NoError) {
        QString errorString = QString("Network error: %1").arg(reply->errorString());
        reply->deleteLater();
        return errorString;
    }

    QByteArray responseData = reply->readAll();
    reply->deleteLater();

    return parseTranslationResponse(responseData);
}

void Translator::translateTextAsync(const QString& text, const QString& targetLanguage)
{
    if (text.isEmpty()) {
        emit translationError("Text is empty");
        return;
    }

    // 获取API Key
    QString apiKey = config.getDeepseekApiKey();

    if (apiKey.isEmpty()) {
        emit translationError("API Key not configured");
        return;
    }

    // 构建网络请求
    QNetworkRequest request;
    request.setUrl(QUrl("https://api.deepseek.com/v1/chat/completions"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(apiKey).toUtf8());

    // 构建请求JSON
    QString requestJson = buildRequestJson(text, targetLanguage);

    // 设置异步模式并发送请求
    m_asyncMode = true;
    m_networkManager->post(request, requestJson.toUtf8());
}

QString Translator::buildRequestJson(const QString& text, const QString& targetLanguage)
{
    QJsonObject requestObj;
    requestObj["model"] = "deepseek-chat";

    // 构建消息数组
    QJsonArray messagesArray;

    QJsonObject systemMessage;
    systemMessage["role"] = "system";
    systemMessage["content"] = QString("你是一个专业的翻译助手。请将用户输入的内容翻译成%1，只返回翻译结果，不要添加任何解释或额外内容。").arg(targetLanguage);
    messagesArray.append(systemMessage);

    QJsonObject userMessage;
    userMessage["role"] = "user";
    userMessage["content"] = text;
    messagesArray.append(userMessage);

    requestObj["messages"] = messagesArray;
    requestObj["temperature"] = 0.3;
    requestObj["max_tokens"] = 2000;
    requestObj["stream"] = false;

    QJsonDocument doc(requestObj);
    return doc.toJson(QJsonDocument::Compact);
}

QString Translator::parseTranslationResponse(const QByteArray& responseData)
{
    QJsonDocument doc = QJsonDocument::fromJson(responseData);
    if (doc.isNull()) {
        return "Error: Invalid JSON response";
    }

    QJsonObject rootObj = doc.object();

    // 检查错误
    if (rootObj.contains("error")) {
        QJsonObject errorObj = rootObj["error"].toObject();
        QString errorMessage = errorObj["message"].toString("Unknown error");
        return QString("API Error: %1").arg(errorMessage);
    }

    // 解析翻译结果
    if (rootObj.contains("choices")) {
        QJsonArray choicesArray = rootObj["choices"].toArray();
        if (!choicesArray.isEmpty()) {
            QJsonObject choiceObj = choicesArray[0].toObject();
            if (choiceObj.contains("message")) {
                QJsonObject messageObj = choiceObj["message"].toObject();
                if (messageObj.contains("content")) {
                    QString translatedText = messageObj["content"].toString().trimmed();
                    return translatedText;
                }
            }
        }
    }

    return "Error: No translation result found";
}

void Translator::onReplyFinished(QNetworkReply* reply)
{
    if (!m_asyncMode) {
        reply->deleteLater();
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        emit translationError(QString("Network error: %1").arg(reply->errorString()));
        reply->deleteLater();
        m_asyncMode = false;
        return;
    }

    QByteArray responseData = reply->readAll();
    QString translatedText = parseTranslationResponse(responseData);

    if (translatedText.startsWith("Error:")) {
        emit translationError(translatedText);
    } else {
        emit translationFinished(translatedText);
    }

    reply->deleteLater();
    m_asyncMode = false;
}
