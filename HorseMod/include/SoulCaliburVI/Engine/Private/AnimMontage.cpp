#include "AnimMontage.h"

UAnimMontage::UAnimMontage() {
    this->BlendInTime = -1.00f;
    this->BlendOutTime = -1.00f;
    this->BlendOutTriggerTime = -1.00f;
    this->SyncSlotIndex = 0;
    this->SlotAnimTracks.AddDefaulted(1);
    this->bEnableRootMotionTranslation = false;
    this->bEnableRootMotionRotation = false;
    this->RootMotionRootLock = ERootMotionRootLock::RefPose;
}


