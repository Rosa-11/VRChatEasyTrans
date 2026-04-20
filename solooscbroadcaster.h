#ifndef SOLOOSCBROADCASTER_H
#define SOLOOSCBROADCASTER_H

#include <QString>
#include <QObject>
#include <QUdpSocket>

class SoloOscBroadcaster : public QObject
{
private:
    QHostAddress targetHost;
    quint16 targetPort;
public:
    SoloOscBroadcaster();

public slots:
    void initialize();
    void sendToOSC(const QString& text);
};

#endif // SOLOOSCBROADCASTER_H
