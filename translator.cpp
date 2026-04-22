#include "Translator.h"
#include "ConfigManager.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>

namespace {
const QString API_URL           = "https://api.deepseek.com/v1/chat/completions";
const int     REQUEST_TIMEOUT_MS = 30000;  // 请求超时时间（毫秒）
}

// ─────────────────────────────────────────────────────────────────────────────
// 构造函数
// ─────────────────────────────────────────────────────────────────────────────
Translator::Translator(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
    // 将网络请求完成信号连接到处理槽
    connect(m_networkManager, &QNetworkAccessManager::finished,
            this, &Translator::onReplyFinished);
}

// ─────────────────────────────────────────────────────────────────────────────
// initialize() — 从 ConfigManager 加载目标语言和 API Key
// 由主窗口 __start__ 信号触发
// ─────────────────────────────────────────────────────────────────────────────
void Translator::initialize()
{
    targetLanguage = ConfigManager::getInstance().getTargetLanguage();
    apiKey         = ConfigManager::getInstance().getDeepseekApiKey();
    emit debug(QString("Translator initialized, target language: %1").arg(targetLanguage));
}

// ─────────────────────────────────────────────────────────────────────────────
// translateTextAsync() — 发起异步翻译请求
// ─────────────────────────────────────────────────────────────────────────────
void Translator::translateTextAsync(const QString& text)
{
    if (text.isEmpty()) {
        emit translationError("Translator: text is empty");
        return;
    }
    if (apiKey.isEmpty()) {
        emit translationError("Translator: DeepSeek API key not configured");
        return;
    }

    // 【修复】如果上一个请求还没完成，先中止它，避免 m_pendingReply 悬挂。
    // 场景：用户说话非常快，前一句话的翻译还没返回，下一句已经来了。
    if (m_pendingReply) {
        emit debug("Translator: aborting previous pending request");
        m_pendingReply->abort();   // 触发 finished 信号，onReplyFinished 里会处理
        m_pendingReply = nullptr;
    }

    QNetworkRequest request;
    request.setUrl(QUrl(API_URL));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization",
                         QString("Bearer %1").arg(apiKey).toUtf8());

    const QByteArray body = buildRequestJson(text, targetLanguage).toUtf8();

    // 保存 reply 指针，用于后续识别回调归属
    m_pendingReply = m_networkManager->post(request, body);

    emit debug(QString("Translator: request sent for text: %1").arg(text));
}

// ─────────────────────────────────────────────────────────────────────────────
// buildRequestJson() — 构造发往 DeepSeek API 的 JSON 请求体
// ─────────────────────────────────────────────────────────────────────────────
QString Translator::buildRequestJson(const QString& text, const QString& targetLang) const
{
    // 【修复】原代码在函数内部构建了两套 messages，最终使用的那套
    // system content 是 QString("...").arg(targetLanguage)，
    // 其中 "..." 是字面量而非真正的提示词，导致 DeepSeek 收到的系统提示完全无效。
    // 现在只构建一套，且提示词内容完整。

    // 系统提示：告诉 DeepSeek 它的角色和输出格式
    const QString systemContent = QString(
                                      "你是一个专业的翻译助手。"
                                      "请将用户输入的内容翻译成%1，"
                                      "只返回翻译结果，不要添加任何解释、标注或额外内容。"
                                      ).arg(targetLang);

    // 构造 messages 数组
    QJsonArray messages;
    messages.append(QJsonObject{
        {"role",    "system"},
        {"content", systemContent}
    });
    messages.append(QJsonObject{
        {"role",    "user"},
        {"content", text}
    });

    // 构造完整请求体
    QJsonObject requestObj{
        {"model",       "deepseek-chat"},
        {"messages",    messages},         // 使用上面构造的 messages，不重复定义
        {"temperature", 0.3},              // 较低的温度，翻译结果更稳定
        {"max_tokens",  2000},
        {"stream",      false}
    };

    return QJsonDocument(requestObj).toJson(QJsonDocument::Compact);
}

// ─────────────────────────────────────────────────────────────────────────────
// parseTranslationResponse() — 从 API 返回的 JSON 中提取翻译文本
// ─────────────────────────────────────────────────────────────────────────────
QString Translator::parseTranslationResponse(const QByteArray& responseData) const
{
    const QJsonDocument doc = QJsonDocument::fromJson(responseData);
    if (doc.isNull()) {
        return "Error: invalid JSON response";
    }

    const QJsonObject rootObj = doc.object();

    // 检查 API 层面的错误（如 key 无效、余额不足等）
    if (rootObj.contains("error")) {
        const QString errMsg = rootObj["error"].toObject()["message"].toString("unknown error");
        return QString("API Error: %1").arg(errMsg);
    }

    // 提取 choices[0].message.content
    const QJsonArray choices = rootObj["choices"].toArray();
    if (!choices.isEmpty()) {
        const QString content = choices.first().toObject()
        ["message"].toObject()
            ["content"].toString().trimmed();
        if (!content.isEmpty()) return content;
    }

    return "Error: no translation result found";
}

// ─────────────────────────────────────────────────────────────────────────────
// onReplyFinished() — 网络请求完成回调
// ─────────────────────────────────────────────────────────────────────────────
void Translator::onReplyFinished(QNetworkReply* reply)
{
    // 延迟删除 reply 对象（Qt 要求在槽函数里不能直接 delete sender 相关对象）
    reply->deleteLater();

    // 【修复】通过指针比对识别回调归属：
    // 原来用 m_asyncMode bool 标志，多个并发请求时第一个 reply 回来会把 m_asyncMode 清零，
    // 后续 reply 回来时直接 return，翻译结果丢失。
    // 现在改为：只处理当前 m_pendingReply，其余（已被 abort 的旧请求）直接忽略。
    if (reply != m_pendingReply) {
        // 这是一个已被 abort 的旧请求，忽略
        emit debug("Translator: ignoring stale reply");
        return;
    }
    // 清空 pendingReply，允许下一次请求
    m_pendingReply = nullptr;

    // 处理网络层错误（连接超时、abort 等）
    if (reply->error() != QNetworkReply::NoError) {
        // 用户主动 abort 的请求不需要报错（是我们自己取消的）
        if (reply->error() != QNetworkReply::OperationCanceledError) {
            emit translationError(QString("Translator: network error: %1")
                                      .arg(reply->errorString()));
        }
        return;
    }

    // 解析 API 响应
    const QByteArray responseData  = reply->readAll();
    const QString    translatedText = parseTranslationResponse(responseData);

    if (translatedText.startsWith("Error:") || translatedText.startsWith("API Error:")) {
        emit translationError(translatedText);
    } else {
        emit debug(QString("Translator: translation done: %1").arg(translatedText));
        emit translationFinished(translatedText);
    }
}
