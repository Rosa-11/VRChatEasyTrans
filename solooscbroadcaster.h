#ifndef SOLOOSCBROADCASTER_H
#define SOLOOSCBROADCASTER_H

#include <QString>
#include "ConfigManager.h"


class SoloOscBroadcaster
{
private:
    ConfigManager &config = ConfigManager::getInstance();
public:
    SoloOscBroadcaster();
    void sendToOSC(const QString& text);
};

#endif // SOLOOSCBROADCASTER_H
