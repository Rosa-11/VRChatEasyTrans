#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <QString>
#include <QSettings>
#include <QObject>
#include <QVariant>

class ConfigManager : public QObject
{
    Q_OBJECT

public:
    explicit ConfigManager(QObject *parent = nullptr);
    ~ConfigManager();

    // 单例模式访问
    static ConfigManager& instance();

    QString baiduApiKey;
    QString baiduSecretKey;
    QString deviceId;
    QString baiDuToken;


signals:
    void configChanged(const QString& key, const QVariant& value);

private:


};

#endif // CONFIGMANAGER_H
