#include "ModelComponent.h"

UModelComponent::UModelComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->Mobility = EComponentMobility::Static;
    this->bGenerateOverlapEvents = false;
    this->bUseAsOccluder = true;
    this->CastShadow = true;
    this->ModelBodySetup = NULL;
}


