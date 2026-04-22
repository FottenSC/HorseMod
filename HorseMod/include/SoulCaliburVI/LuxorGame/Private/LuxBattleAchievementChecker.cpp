#include "LuxBattleAchievementChecker.h"

ALuxBattleAchievementChecker::ALuxBattleAchievementChecker(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->BattleMode = ELuxBattleMode::EBM_DUMMY;
    this->MessageFlags.AddDefaulted(2);
}


