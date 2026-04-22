#include "OnlineBeacon.h"

AOnlineBeacon::AOnlineBeacon(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->NetDriverName = TEXT("BeaconDriver");
    this->bRelevantForNetworkReplays = false;
    this->BeaconConnectionInitialTimeout = 5.00f;
    this->BeaconConnectionTimeout = 45.00f;
    this->NetDriver = NULL;
}


