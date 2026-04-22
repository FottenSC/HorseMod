#include "LuxOptionSaveObject.h"

ULuxOptionSaveObject::ULuxOptionSaveObject() {
    this->KeyConfigType1P = ELuxBattleInputType::TypeA;
    this->KeyConfigType2P = ELuxBattleInputType::TypeA;
}

void ULuxOptionSaveObject::ValidateStageBgmSettingsData() {
}

void ULuxOptionSaveObject::RefreshCurrentDiplayQualitySetting() {
}

ULuxOptionSaveObject* ULuxOptionSaveObject::GetSaveObject(ELuxGameSaveObjectAccessType InAccessType) {
    return NULL;
}

void ULuxOptionSaveObject::ApplyToGameData() {
}

void ULuxOptionSaveObject::ApplyFromGameData() {
}


