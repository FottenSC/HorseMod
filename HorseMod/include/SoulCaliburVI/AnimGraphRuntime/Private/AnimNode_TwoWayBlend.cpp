#include "AnimNode_TwoWayBlend.h"

FAnimNode_TwoWayBlend::FAnimNode_TwoWayBlend() {
    this->Alpha = 0.00f;
    this->InternalBlendAlpha = 0.00f;
    this->bAIsRelevant = false;
    this->bBIsRelevant = false;
    this->bResetChildOnActivation = false;
}

