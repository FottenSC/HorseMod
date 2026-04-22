#include "BlendSpaceBase.h"

UBlendSpaceBase::UBlendSpaceBase() {
    this->bRotationBlendInMeshSpace = false;
    this->AnimLength = 0.00f;
    this->TargetWeightInterpolationSpeedPerSec = 0.00f;
    this->NotifyTriggerMode = ENotifyTriggerMode::AllAnimations;
    this->SampleIndexWithMarkers = -1;
}


