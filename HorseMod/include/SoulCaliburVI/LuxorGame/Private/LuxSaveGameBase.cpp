#include "LuxSaveGameBase.h"

ULuxSaveGameBase::ULuxSaveGameBase() {
    this->SlotName = TEXT("ERROR_SLOT");
}

bool ULuxSaveGameBase::PerformSave(const FString& inSuffix) {
    return false;
}


