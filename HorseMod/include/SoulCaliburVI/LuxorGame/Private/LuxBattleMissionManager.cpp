#include "LuxBattleMissionManager.h"

ALuxBattleMissionManager::ALuxBattleMissionManager(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->bWeaponEnabled = true;
}

void ALuxBattleMissionManager::SetWeaponEnabled(bool bEnabled) {
}

bool ALuxBattleMissionManager::IsWeaponEnabled() const {
    return false;
}

bool ALuxBattleMissionManager::IsSlipEnabled(int32 inPlayerIndex) const {
    return false;
}


