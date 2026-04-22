#include "OculusBoundaryComponent.h"

UOculusBoundaryComponent::UOculusBoundaryComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->bAutoActivate = true;
}

bool UOculusBoundaryComponent::SetOuterBoundaryColor(const FColor InBoundaryColor) {
    return false;
}

bool UOculusBoundaryComponent::ResetOuterBoundaryColor() {
    return false;
}

bool UOculusBoundaryComponent::RequestOuterBoundaryVisible(bool BoundaryVisible) {
    return false;
}

bool UOculusBoundaryComponent::IsOuterBoundaryTriggered() {
    return false;
}

bool UOculusBoundaryComponent::IsOuterBoundaryDisplayed() {
    return false;
}

FBoundaryTestResult UOculusBoundaryComponent::GetTriggeredPlayAreaInfo(ETrackedDeviceType DeviceType) {
    return FBoundaryTestResult{};
}

TArray<FBoundaryTestResult> UOculusBoundaryComponent::GetTriggeredOuterBoundaryInfo() {
    return TArray<FBoundaryTestResult>();
}

TArray<FVector> UOculusBoundaryComponent::GetPlayAreaPoints() {
    return TArray<FVector>();
}

FVector UOculusBoundaryComponent::GetPlayAreaDimensions() {
    return FVector{};
}

TArray<FVector> UOculusBoundaryComponent::GetOuterBoundaryPoints() {
    return TArray<FVector>();
}

FVector UOculusBoundaryComponent::GetOuterBoundaryDimensions() {
    return FVector{};
}

FBoundaryTestResult UOculusBoundaryComponent::CheckIfPointWithinPlayArea(const FVector Point) {
    return FBoundaryTestResult{};
}

FBoundaryTestResult UOculusBoundaryComponent::CheckIfPointWithinOuterBounds(const FVector Point) {
    return FBoundaryTestResult{};
}


