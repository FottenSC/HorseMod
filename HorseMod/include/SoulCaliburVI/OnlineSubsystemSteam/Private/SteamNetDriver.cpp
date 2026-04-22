#include "SteamNetDriver.h"

USteamNetDriver::USteamNetDriver() : UIpNetDriver(FObjectInitializer::Get()) {
    this->NetConnectionClassName = TEXT("/Script/OnlineSubsystemSteam.SteamNetConnection");
}


