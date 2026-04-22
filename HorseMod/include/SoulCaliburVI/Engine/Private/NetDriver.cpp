#include "NetDriver.h"

UNetDriver::UNetDriver() {
    this->MaxDownloadSize = 0;
    this->bClampListenServerTickRate = false;
    this->NetServerMaxTickRate = 0;
    this->MaxInternetClientRate = 10000;
    this->MaxClientRate = 15000;
    this->ServerTravelPause = 0.00f;
    this->SpawnPrioritySeconds = 0.00f;
    this->RelevantTimeout = 0.00f;
    this->KeepAliveTime = 0.00f;
    this->InitialConnectTimeout = 0.00f;
    this->ConnectionTimeout = 0.00f;
    this->TimeoutMultiplierForUnoptimizedBuilds = 0.00f;
    this->bNoTimeouts = false;
    this->ServerConnection = NULL;
    this->World = NULL;
    this->NetConnectionClass = NULL;
    this->RoleProperty = NULL;
    this->RemoteRoleProperty = NULL;
    this->NetDriverName = TEXT("GameNetDriver");
    this->Time = 0.00f;
}


