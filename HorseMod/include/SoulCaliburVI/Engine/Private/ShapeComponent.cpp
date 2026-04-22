#include "ShapeComponent.h"
#include "NavArea_Obstacle.h"

UShapeComponent::UShapeComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->bCanEverAffectNavigation = true;
    this->bHiddenInGame = true;
    this->bCastDynamicShadow = false;
    this->bHasCustomNavigableGeometry = EHasCustomNavigableGeometry::Yes;
    this->ShapeBodySetup = NULL;
    this->bDrawOnlyIfSelected = false;
    this->bShouldCollideWhenPlacing = false;
    this->bDynamicObstacle = false;
    this->AreaClass = UNavArea_Obstacle::StaticClass();
}


