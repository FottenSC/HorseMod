#include "LuxBattleReplaySave.h"

ULuxBattleReplaySave::ULuxBattleReplaySave() {
    this->SlotName = TEXT("REPLAY/");
}

bool ULuxBattleReplaySave::SaveReplayToSlot(const ULuxBattleSetup* inBattleSetup, const FLuxBattleStateResetData& inStateResetData, const FLuxBattleRecordingData& inRecordingData, const FString& inSuffix) {
    return false;
}

ULuxBattleReplaySave* ULuxBattleReplaySave::LoadReplaySave(const FString& inSuffix) {
    return NULL;
}

bool ULuxBattleReplaySave::DoesReplaySaveExist(const FString& inSuffix) {
    return false;
}


