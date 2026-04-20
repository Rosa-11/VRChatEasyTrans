#include "Translator.h"
#include "ConfigManager.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QEventLoop>
#include <QTimer>

namespace {
const QString API_URL = "https://api.deepseek.com/v1/chat/completions";
const int REQUEST_TIMEOUT_MS = 30000;
}

Translator::Translator(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
    connect(m_networkManager, &QNetworkAccessManager::finished,
            this, &Translator::onReplyFinished);
}

void Translator::initialize()
{
    qDebug() << "Translator initialize";
    targetLanguage = ConfigManager::getInstance().getTargetLanguage();
    apiKey = ConfigManager::getInstance().getDeepseekApiKey();
}

void Translator::translateTextAsync(const QString& text)
{
    if (text.isEmpty()) {
        emit translationError("Text is empty");
        return;
    }

    if (apiKey.isEmpty()) {
        emit translationError("API Key not configured");
        return;
    }

    QNetworkRequest request;
    request.setUrl(QUrl(API_URL));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(apiKey).toUtf8());

    const QString requestJson = buildRequestJson(text, targetLanguage);

    m_asyncMode = true;
    m_networkManager->post(request, requestJson.toUtf8());
}

QString Translator::buildRequestJson(const QString& text, const QString& targetLanguage) const
{
    QJsonObject systemMessage;
    systemMessage["role"] = "system";
    systemMessage["content"] = QString("你是一个专业的翻译助手。请将用户输入的内容翻译成%1，"
                                       "只返回翻译结果，不要添加任何解释或额外内容。")
                                   .arg(targetLanguage);

    QJsonObject userMessage;
    userMessage["role"] = "user";
    userMessage["content"] = text;

    QJsonArray messagesArray;
    messagesArray.append(systemMessage);
    messagesArray.append(userMessage);

    QJsonObject requestObj{
        {"model", "deepseek-chat"},
        {"messages", QJsonArray{
            QJsonObject{
                {"role", "system"},
                {"content", QString("...").arg(targetLanguage)}
            },
            QJsonObject{
                {"role", "user"},
                {"content", text}
            }
        }},
        {"temperature", 0.3},
        {"max_tokens", 2000},
        {"stream", false}
    };

    return QJsonDocument(requestObj).toJson(QJsonDocument::Compact);
}

QString Translator::parseTranslationResponse(const QByteArray& responseData) const
{
    QJsonDocument doc = QJsonDocument::fromJson(responseData);
    if (doc.isNull()) {
        return "Error: Invalid JSON response";
    }

    const QJsonObject rootObj = doc.object();

    // 检查 API 返回的错误
    if (rootObj.contains("error")) {
        const QJsonObject errorObj = rootObj["error"].toObject();
        const QString errorMessage = errorObj["message"].toString("Unknown error");
        return QString("API Error: %1").arg(errorMessage);
    }

    // 提取翻译内容
    const QJsonArray choices = rootObj["choices"].toArray();
    if (!choices.isEmpty()) {
        const QJsonObject choice = choices.first().toObject();
        const QJsonObject message = choice["message"].toObject();
        const QString content = message["content"].toString().trimmed();
        if (!content.isEmpty()) {
            return content;
        }
    }

    return "Error: No translation result found";
}

void Translator::onReplyFinished(QNetworkReply* reply)
{
    reply->deleteLater();

    if (!m_asyncMode) {
        return;
    }
    m_asyncMode = false;

    if (reply->error() != QNetworkReply::NoError) {
        emit translationError(QString("Network error: %1").arg(reply->errorString()));
        return;
    }

    const QByteArray responseData = reply->readAll();
    const QString translatedText = parseTranslationResponse(responseData);

    if (translatedText.startsWith("Error:")) {
        emit translationError(translatedText);
    } else {
        emit translationFinished(translatedText);   ///////////////
    }
}
