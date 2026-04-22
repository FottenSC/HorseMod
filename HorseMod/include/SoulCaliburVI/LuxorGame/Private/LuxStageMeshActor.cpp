#include "LuxStageMeshActor.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=StaticMeshComponent -FallbackName=StaticMeshComponent
#include "LuxStageMeshComponent.h"

ALuxStageMeshActor::ALuxStageMeshActor(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->RootComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BaseMesh"));
    this->BaseMeshComponent = (UStaticMeshComponent*)RootComponent;
    this->SoftOpacityMeshComponent = CreateDefaultSubobject<ULuxStageMeshComponent>(TEXT("SoftOpacityMesh"));
    this->TranslucentMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TranslucentMesh"));
    this->FadeFrame = 10;
    this->TestEnabled = false;
    this->SoftOpacityMeshComponent->SetupAttachment(RootComponent);
    this->TranslucentMeshComponent->SetupAttachment(RootComponent);
}


