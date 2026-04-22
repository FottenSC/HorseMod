#include "ProjectileMovementComponent.h"

UProjectileMovementComponent::UProjectileMovementComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->InitialSpeed = 0.00f;
    this->MaxSpeed = 0.00f;
    this->bRotationFollowsVelocity = false;
    this->bShouldBounce = false;
    this->bInitialVelocityInLocalSpace = true;
    this->bForceSubStepping = false;
    this->bIsHomingProjectile = false;
    this->bBounceAngleAffectsFriction = false;
    this->bIsSliding = false;
    this->PreviousHitTime = 1.00f;
    this->ProjectileGravityScale = 1.00f;
    this->Buoyancy = 0.00f;
    this->Bounciness = 0.60f;
    this->Friction = 0.20f;
    this->BounceVelocityStopSimulatingThreshold = 5.00f;
    this->HomingAccelerationMagnitude = 0.00f;
    this->MaxSimulationTimeStep = 0.05f;
    this->MaxSimulationIterations = 8;
}

void UProjectileMovementComponent::StopSimulating(const FHitResult& HitResult) {
}

void UProjectileMovementComponent::SetVelocityInLocalSpace(FVector NewVelocity) {
}

FVector UProjectileMovementComponent::LimitVelocity(FVector NewVelocity) const {
    return FVector{};
}


