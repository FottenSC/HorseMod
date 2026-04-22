#include "MotionControllerComponent.h"

UMotionControllerComponent::UMotionControllerComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->bAutoActivate = true;
    this->PlayerIndex = 0;
    this->Hand = EControllerHand::Left;
    this->bDisableLowLatencyUpdate = false;
    this->CurrentTrackingStatus = ETrackingStatus::NotTracked;
}

bool UMotionControllerComponent::IsTracked() const {
    return false;
}


