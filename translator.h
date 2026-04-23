#ifndef TRANSLATOR_H
#define TRANSLATOR_H

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class Translator : public QObject
{
    Q_OBJECT

public:
    explicit Translator(QObject *parent = nullptr);

public slots:
    // 初始化：从 ConfigManager 加载目标语言和 API Key
    void initialize();

    // 发起异步翻译请求（由 SpeechRecogniser::recognitionCompleted 触发）
    void translateTextAsync(const QString& text);

signals:
    // 翻译完成，发出翻译结果（由 SoloOscBroadcaster::sendToOSC 接收）
    void translationFinished(const QString& translatedText);

    // 翻译出错
    void translationError(const QString& errorMessage);

    void debug(const QString& debugMessage);

private slots:
    // QNetworkAccessManager::finished 信号的回调
    void onReplyFinished(QNetworkReply* reply);

private:
    // 构造 DeepSeek API 请求 JSON 体
    QString buildRequestJson(const QString& text, const QString& targetLang) const;

    // 从 API 响应 JSON 中提取翻译文本
    QString parseTranslationResponse(const QByteArray& responseData) const;

    QNetworkAccessManager* m_networkManager = nullptr;

    // 用指针追踪当前进行中的请求，替代原来的 bool m_asyncMode。
    // 原 m_asyncMode 在多个并发请求时会导致后续结果被丢弃。
    // 现在通过比对 reply 指针判断回调归属，旧的（已 abort）请求直接忽略。
    QNetworkReply* m_pendingReply = nullptr;

    QString m_originalText;  // 保存原文，用于和译文组合输出

    QString targetLanguage;  // 目标翻译语言（如 "英语"、"日语"）
    QString apiKey;          // DeepSeek API Key
};

#endif // TRANSLATOR_H
