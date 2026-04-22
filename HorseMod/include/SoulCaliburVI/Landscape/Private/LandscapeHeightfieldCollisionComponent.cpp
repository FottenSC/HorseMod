#include "LandscapeHeightfieldCollisionComponent.h"

ULandscapeHeightfieldCollisionComponent::ULandscapeHeightfieldCollisionComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->bCanEverAffectNavigation = true;
    this->Mobility = EComponentMobility::Static;
    this->bGenerateOverlapEvents = false;
    this->bAllowCullDistanceVolume = false;
    this->bUseAsOccluder = true;
    this->bHasCustomNavigableGeometry = EHasCustomNavigableGeometry::Yes;
    this->SectionBaseX = 0;
    this->SectionBaseY = 0;
    this->CollisionSizeQuads = 0;
    this->CollisionScale = 0.00f;
    this->SimpleCollisionSizeQuads = 0;
}


