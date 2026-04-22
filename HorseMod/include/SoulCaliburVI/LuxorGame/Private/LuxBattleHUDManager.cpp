#include "LuxBattleHUDManager.h"

ALuxBattleHUDManager::ALuxBattleHUDManager(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->CockpitEventListener = NULL;
    this->NoticeEventListener = NULL;
    this->DamageInfoEventListener = NULL;
    this->InputKeyEventListener = NULL;
    this->PersonalityCommentEventListener = NULL;
    this->HUDController = NULL;
}


