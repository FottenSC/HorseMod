#include "IpNetDriver.h"

UIpNetDriver::UIpNetDriver() {
    this->NetConnectionClassName = TEXT("/Script/OnlineSubsystemUtils.IpConnection");
    this->LogPortUnreach = false;
    this->AllowPlayerPortUnreach = false;
    this->MaxPortCountToTry = 512;
}


