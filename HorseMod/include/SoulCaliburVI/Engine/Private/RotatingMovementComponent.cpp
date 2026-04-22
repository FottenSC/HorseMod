#include "RotatingMovementComponent.h"

URotatingMovementComponent::URotatingMovementComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->bRotationInLocalSpace = true;
}


