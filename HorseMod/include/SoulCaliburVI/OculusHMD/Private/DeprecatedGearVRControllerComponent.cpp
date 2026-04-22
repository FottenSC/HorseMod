#include "DeprecatedGearVRControllerComponent.h"

UDEPRECATED_DeprecatedGearVRControllerComponent::UDEPRECATED_DeprecatedGearVRControllerComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->bAutoActivate = true;
}

UMotionControllerComponent* UDEPRECATED_DeprecatedGearVRControllerComponent::GetMotionController() const {
    return NULL;
}

UStaticMeshComponent* UDEPRECATED_DeprecatedGearVRControllerComponent::GetControllerMesh() const {
    return NULL;
}


