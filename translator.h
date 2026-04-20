#ifndef TRANSLATOR_H
#define TRANSLATOR_H

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>

class QNetworkReply;

class Translator : public QObject
{
    Q_OBJECT

private:
    QNetworkAccessManager* m_networkManager = nullptr;
    bool m_asyncMode = false;

    QString targetLanguage = "";
    QString apiKey = "";

    QString buildRequestJson(const QString& text, const QString& targetLanguage) const;
    QString parseTranslationResponse(const QByteArray& responseData) const;

private slots:
    void onReplyFinished(QNetworkReply* reply);

public:
    explicit Translator(QObject *parent = nullptr);

public slots:
    void initialize();
    void translateTextAsync(const QString& text); // 翻译完成信号对应的槽函数

signals:
    void translationFinished(const QString& translatedText);    ////////////////
    void translationError(const QString& errorMessage);
    void debug(const QString& debugMessage);
};

#endif // TRANSLATOR_H
