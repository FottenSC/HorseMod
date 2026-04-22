#include "LuxBattleStageShipCameraActor.h"

ALuxBattleStageShipCameraActor::ALuxBattleStageShipCameraActor(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->Range = 0.30f;
    this->bIsFighting = false;
}


