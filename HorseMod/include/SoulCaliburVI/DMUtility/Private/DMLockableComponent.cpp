#include "DMLockableComponent.h"

UDMLockableComponent::UDMLockableComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->LockPriority = 1.00f;
    this->ShapeMaterial = NULL;
    this->SphereRadius = 100.00f;
    this->LockableBodySetup = NULL;
}


