#include "AnimNode_BlendListBase.h"

FAnimNode_BlendListBase::FAnimNode_BlendListBase() {
    this->BlendType = EAlphaBlendOption::Linear;
    this->CustomBlendCurve = NULL;
    this->BlendProfile = NULL;
    this->LastActiveChildIndex = 0;
    this->bResetChildOnActivation = false;
}

