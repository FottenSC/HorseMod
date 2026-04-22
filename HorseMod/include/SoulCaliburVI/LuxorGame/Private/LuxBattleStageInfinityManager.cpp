#include "LuxBattleStageInfinityManager.h"

ALuxBattleStageInfinityManager::ALuxBattleStageInfinityManager(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->MapChipSize = 0;
    this->MapChiptilingCount = 0;
    this->MaxDistanecFromOrigin = 0;
    this->IsInfinityStage = false;
    this->bNowOnPositionReset = false;
    this->WaitOneFrameForFadeIn = 0;
    this->FadeManager = NULL;
    this->StageTileUpdateIntervalCounter = 0.00f;
    this->PlayerOrginActor = NULL;
}

void ALuxBattleStageInfinityManager::OnFadeOutAnimFinished() {
}

void ALuxBattleStageInfinityManager::OnFadeInAnimFinished() {
}


