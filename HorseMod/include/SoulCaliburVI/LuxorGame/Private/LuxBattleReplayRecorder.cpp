#include "LuxBattleReplayRecorder.h"

ALuxBattleReplayRecorder::ALuxBattleReplayRecorder(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->bIsRecording = false;
    this->CurrentRound = 0;
    this->CurrentTime = 0;
}


