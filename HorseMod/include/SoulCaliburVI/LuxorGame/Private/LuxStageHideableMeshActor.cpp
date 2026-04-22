#include "LuxStageHideableMeshActor.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=StaticMeshComponent -FallbackName=StaticMeshComponent
#include "LuxStageMeshComponent.h"

ALuxStageHideableMeshActor::ALuxStageHideableMeshActor(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->RootComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BaseMesh"));
    this->BaseMeshComponent = (UStaticMeshComponent*)RootComponent;
    this->DitherMeshComponent = CreateDefaultSubobject<ULuxStageMeshComponent>(TEXT("DitherMesh"));
    this->bDitherEnabled = false;
    this->DitherFrame = 10;
    this->DitherMeshComponent->SetupAttachment(RootComponent);
}

void ALuxStageHideableMeshActor::SetMeshHidden(bool inHidden) {
}

void ALuxStageHideableMeshActor::Initialize() {
}

void ALuxStageHideableMeshActor::Finalize() {
}


