#include "DemoNetDriver.h"

UDemoNetDriver::UDemoNetDriver() {
    this->NetConnectionClassName = TEXT("/Script/Engine.DemoNetConnection");
    this->CheckpointSaveMaxMSPerFrame = 0.00f;
    this->bIsLocalReplay = false;
}


