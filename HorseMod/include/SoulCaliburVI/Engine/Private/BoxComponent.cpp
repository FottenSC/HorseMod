#include "BoxComponent.h"

UBoxComponent::UBoxComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->bUseEditorCompositing = true;
}

void UBoxComponent::SetBoxExtent(FVector InBoxExtent, bool bUpdateOverlaps) {
}

FVector UBoxComponent::GetUnscaledBoxExtent() const {
    return FVector{};
}

FVector UBoxComponent::GetScaledBoxExtent() const {
    return FVector{};
}


