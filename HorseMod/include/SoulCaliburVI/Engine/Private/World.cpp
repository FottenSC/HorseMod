#include "World.h"

UWorld::UWorld() {
    this->PersistentLevel = NULL;
    this->NetDriver = NULL;
    this->LineBatcher = NULL;
    this->PersistentLineBatcher = NULL;
    this->ForegroundLineBatcher = NULL;
    this->NetworkManager = NULL;
    this->PhysicsCollisionHandler = NULL;
    this->CurrentLevelPendingVisibility = NULL;
    this->CurrentLevelPendingInvisibility = NULL;
    this->DemoNetDriver = NULL;
    this->MyParticleEventManager = NULL;
    this->DefaultPhysicsVolume = NULL;
    this->NavigationSystem = NULL;
    this->AuthorityGameMode = NULL;
    this->GameState = NULL;
    this->AISystem = NULL;
    this->AvoidanceManager = NULL;
    this->CurrentLevel = NULL;
    this->OwningGameInstance = NULL;
    this->CanvasForRenderingToTarget = NULL;
    this->CanvasForDrawMaterialToRenderTarget = NULL;
    this->WorldComposition = NULL;
    this->bAreConstraintsDirty = false;
}


