#include "LuxBattleReplayPlayer.h"

ALuxBattleReplayPlayer::ALuxBattleReplayPlayer(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->bIsPlayingBack = false;
    this->CurrentRound = 0;
    this->CurrentTime = 0;
}

bool ALuxBattleReplayPlayer::IsExistNextRound() const {
    return false;
}


