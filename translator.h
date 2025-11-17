#ifndef TRANSLATOR_H
#define TRANSLATOR_H

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QEventLoop>
#include <QTimer>

class ConfigManager;

class Translator : public QObject
{
    Q_OBJECT

public:
    explicit Translator(QObject *parent = nullptr);

    // 翻译文本
    QString translateText(const QString& text, const QString& targetLanguage = "English");

    // 异步翻译
    void translateTextAsync(const QString& text, const QString& targetLanguage = "English");

signals:
    void translationFinished(const QString& translatedText);
    void translationError(const QString& errorMessage);

private slots:
    void onReplyFinished(QNetworkReply* reply);

private:
    ConfigManager& config;
    QNetworkAccessManager* m_networkManager;
    QString m_lastTranslation;
    bool m_asyncMode;

    QString buildRequestJson(const QString& text, const QString& targetLanguage);
    QString parseTranslationResponse(const QByteArray& responseData);
};

#endif // TRANSLATOR_H
