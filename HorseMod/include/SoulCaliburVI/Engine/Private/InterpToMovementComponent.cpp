#include "InterpToMovementComponent.h"

UInterpToMovementComponent::UInterpToMovementComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->Duration = 1.00f;
    this->bPauseOnImpact = false;
    this->BehaviourType = EInterpToBehaviourType::OneShot;
    this->bForceSubStepping = false;
    this->MaxSimulationTimeStep = 0.05f;
    this->MaxSimulationIterations = 8;
}

void UInterpToMovementComponent::StopSimulating(const FHitResult& HitResult) {
}

void UInterpToMovementComponent::RestartMovement(float InitialDirection) {
}

void UInterpToMovementComponent::FinaliseControlPoints() {
}


