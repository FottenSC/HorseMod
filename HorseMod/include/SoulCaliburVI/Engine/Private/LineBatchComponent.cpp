#include "LineBatchComponent.h"

ULineBatchComponent::ULineBatchComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->bAutoActivate = true;
    this->bGenerateOverlapEvents = false;
    this->bUseEditorCompositing = true;
}


