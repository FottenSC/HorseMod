#include "PacketSimulationSettings.h"

FPacketSimulationSettings::FPacketSimulationSettings() {
    this->PktLoss = 0;
    this->PktOrder = 0;
    this->PktDup = 0;
    this->PktLag = 0;
    this->PktLagVariance = 0;
}

