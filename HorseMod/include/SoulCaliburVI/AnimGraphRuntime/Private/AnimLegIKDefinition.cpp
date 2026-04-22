#include "AnimLegIKDefinition.h"

FAnimLegIKDefinition::FAnimLegIKDefinition() {
    this->NumBonesInLimb = 0;
    this->FootBoneForwardAxis = EAxis::None;
    this->bEnableRotationLimit = false;
    this->MinRotationAngle = 0.00f;
    this->bEnableKneeTwistCorrection = false;
}

