#include "NavLinkComponent.h"

UNavLinkComponent::UNavLinkComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->bCanEverAffectNavigation = true;
    this->Mobility = EComponentMobility::Stationary;
    this->bGenerateOverlapEvents = false;
    this->bHasCustomNavigableGeometry = EHasCustomNavigableGeometry::EvenIfNotCollidable;
    this->Links.AddDefaulted(1);
}


