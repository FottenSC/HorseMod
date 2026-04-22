#include "ControlPointMeshComponent.h"

UControlPointMeshComponent::UControlPointMeshComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->Mobility = EComponentMobility::Static;
}


