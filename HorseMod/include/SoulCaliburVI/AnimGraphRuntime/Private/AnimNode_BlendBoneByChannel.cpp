#include "AnimNode_BlendBoneByChannel.h"

FAnimNode_BlendBoneByChannel::FAnimNode_BlendBoneByChannel() {
    this->Alpha = 0.00f;
    this->TransformsSpace = BCS_WorldSpace;
    this->InternalBlendAlpha = 0.00f;
    this->bBIsRelevant = false;
}

