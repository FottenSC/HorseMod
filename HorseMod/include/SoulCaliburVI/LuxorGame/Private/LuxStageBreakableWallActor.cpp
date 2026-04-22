#include "LuxStageBreakableWallActor.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=StaticMeshComponent -FallbackName=StaticMeshComponent
#include "LuxSkeletalMeshComponent.h"
#include "LuxStageMeshComponent.h"
#include "LuxStageSkeletalMeshComponent.h"

ALuxStageBreakableWallActor::ALuxStageBreakableWallActor(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->RootComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BaseMesh"));
    this->BaseMeshComponent = (UStaticMeshComponent*)RootComponent;
    this->BaseTranslucentMeshComponent = CreateDefaultSubobject<ULuxStageMeshComponent>(TEXT("BaseTranslucentMesh"));
    this->BrokenMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BrokenMesh"));
    this->BrokenTranslucentMeshComponent = CreateDefaultSubobject<ULuxStageMeshComponent>(TEXT("BrokenTranslucentMesh"));
    this->BreakingMeshComponent = CreateDefaultSubobject<ULuxSkeletalMeshComponent>(TEXT("BreakingMeshComponent"));
    this->BreakingTranslucentMeshComponent = CreateDefaultSubobject<ULuxStageSkeletalMeshComponent>(TEXT("BreakingTranslucentMeshComponent"));
    this->ID = -1;
    this->FadeFrame = 10;
    this->ParticleSystem = NULL;
    this->ParticleSystemComponent = NULL;
    this->BaseTranslucentMeshComponent->SetupAttachment(RootComponent);
    this->BreakingMeshComponent->SetupAttachment(RootComponent);
    this->BreakingTranslucentMeshComponent->SetupAttachment(RootComponent);
    this->BrokenMeshComponent->SetupAttachment(RootComponent);
    this->BrokenTranslucentMeshComponent->SetupAttachment(RootComponent);
}

void ALuxStageBreakableWallActor::OnParticleSystemFinished(UParticleSystemComponent* FinishedPSComponent) {
}

void ALuxStageBreakableWallActor::Broken(bool Immediately) {
}


