#include "LuxBattleSubtitleManager.h"

ALuxBattleSubtitleManager::ALuxBattleSubtitleManager(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->SubtitleControllerClass = NULL;
    this->SubtitleControllerInstance = NULL;
    this->BattleEnshutsuSubtitle = NULL;
    this->DevBattleSubtitleSetting = NULL;
}


