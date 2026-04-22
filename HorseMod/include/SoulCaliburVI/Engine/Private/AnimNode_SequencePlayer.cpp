#include "AnimNode_SequencePlayer.h"

FAnimNode_SequencePlayer::FAnimNode_SequencePlayer() {
    this->Sequence = NULL;
    this->bLoopAnimation = false;
    this->PlayRate = 0.00f;
    this->StartPosition = 0.00f;
}

