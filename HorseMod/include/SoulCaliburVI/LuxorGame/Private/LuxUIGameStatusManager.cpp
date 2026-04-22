#include "LuxUIGameStatusManager.h"

ULuxUIGameStatusManager::ULuxUIGameStatusManager() {
    this->LoadingIconInstance = NULL;
    this->SavingIconHandle = NULL;
    this->SavingIconInstance = NULL;
    this->LoadingIconWidget = NULL;
    this->SavingIconWidget = NULL;
}

void ULuxUIGameStatusManager::OnSaveEvent(int32 EventType) {
}

bool ULuxUIGameStatusManager::IsVisible(ELuxUIGameStatusIconType inType) const {
    return false;
}

ULuxUIGameStatusIconHandle* ULuxUIGameStatusManager::CreateIcon(ELuxUIGameStatusIconType inType) {
    return NULL;
}


