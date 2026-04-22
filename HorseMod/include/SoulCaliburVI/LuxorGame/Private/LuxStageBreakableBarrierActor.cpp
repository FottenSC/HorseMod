#include "LuxStageBreakableBarrierActor.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=StaticMeshComponent -FallbackName=StaticMeshComponent
#include "LuxSkeletalMeshComponent.h"

ALuxStageBreakableBarrierActor::ALuxStageBreakableBarrierActor(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->RootComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BarrierFace"));
    this->BarrierFace = (UStaticMeshComponent*)RootComponent;
    this->BarrierBack = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BarrierBack"));
    this->BarrierFloor = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BarrierFloor"));
    this->BarrierBreaking = CreateDefaultSubobject<ULuxSkeletalMeshComponent>(TEXT("BarrierBreaking"));
    this->ID = -1;
    this->Endurance = 1;
    this->FadeFrame = 15;
    this->BreakPS = NULL;
    this->HitEffect = NULL;
    this->BreakEffect = NULL;
    this->MID_Face = NULL;
    this->MID_Back = NULL;
    this->MID_Floor = NULL;
    this->MID_Breaking = NULL;
    this->BarrierBack->SetupAttachment(RootComponent);
    this->BarrierBreaking->SetupAttachment(RootComponent);
    this->BarrierFloor->SetupAttachment(RootComponent);
}

void ALuxStageBreakableBarrierActor::OnParticleSystemFinished(UParticleSystemComponent* FinishedPSComponent) {
}

void ALuxStageBreakableBarrierActor::Initialize(int32 HitCount) {
}

void ALuxStageBreakableBarrierActor::Hit(const FVector& Position) {
}


