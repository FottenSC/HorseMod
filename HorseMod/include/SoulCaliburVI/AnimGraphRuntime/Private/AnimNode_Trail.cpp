#include "AnimNode_Trail.h"

FAnimNode_Trail::FAnimNode_Trail() {
    this->ChainLength = 0;
    this->ChainBoneAxis = EAxis::None;
    this->bInvertChainBoneAxis = false;
    this->TrailRelaxation = 0.00f;
    this->bLimitStretch = false;
    this->StretchLimit = 0.00f;
    this->bActorSpaceFakeVel = false;
}

