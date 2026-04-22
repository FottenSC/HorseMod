#include "LuxBattleTrainingReplayPlayer.h"

ALuxBattleTrainingReplayPlayer::ALuxBattleTrainingReplayPlayer(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->bIsPlaying = false;
}

void ALuxBattleTrainingReplayPlayer::RequestToStop() {
}

void ALuxBattleTrainingReplayPlayer::RequestToPlay(bool bLoop) {
}

bool ALuxBattleTrainingReplayPlayer::IsPlaying() const {
    return false;
}

bool ALuxBattleTrainingReplayPlayer::IsExisting() const {
    return false;
}

void ALuxBattleTrainingReplayPlayer::ClearShortReplay() {
}


