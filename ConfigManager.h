#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <QString>
#include <QSettings>
#include <QObject>
#include <QVariant>

class ConfigManager : public QObject
{
    Q_OBJECT
    friend class MainWindow;

public:
    explicit ConfigManager(QObject *parent = nullptr);
    ~ConfigManager();

    // 单例模式访问
    static ConfigManager& instance();

    // QString baiduApiKey;
    // QString baiduSecretKey;
    // QString deviceId;
    // QString baiDuToken;

    // 对应的getter和setter方法
    void setXunFeiConfig(const QString& appId, const QString& apiKey, const QString& apiSecret);
    QString getXunFeiAppId() const { return xunFeiAppId; }
    QString getXunFeiApiKey() const { return xunFeiApiKey; }
    QString getXunFeiApiSecret() const { return xunFeiApiSecret; }

    QString getDeepseekApiKey() const { return DeepseekApiKey; }

    float getVadThreshold() const { return vadThreshold; }
    int getMinSilenceDuration() const {return minSilenceDuration; }
    int getSampleRate() const { return sampleRate; }

signals:
    void configChanged(const QString& key, const QVariant& value);

private:
    // API 配置
    QString xunFeiAppId;      // 讯飞App ID
    QString xunFeiApiKey;     // 讯飞API Key
    QString xunFeiApiSecret;  // 讯飞API Secret

    QString DeepseekApiKey;   // Deepseek API Key

    QString targetLanguage;   // 目标翻译语言

    // 声音录制设置
    float vadThreshold;        // 0~1
    int minSilenceDuration;    // 单位：ms
    int sampleRate;            // 单位：Hz

};

#endif // CONFIGMANAGER_H
