#include "ArrowComponent.h"

UArrowComponent::UArrowComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->bHiddenInGame = true;
    this->bGenerateOverlapEvents = false;
    this->bUseEditorCompositing = true;
    this->ArrowSize = 1.00f;
    this->bIsScreenSizeScaled = false;
    this->ScreenSize = 0.00f;
    this->bTreatAsASprite = false;
}

void UArrowComponent::SetArrowColor(FLinearColor NewColor) {
}


