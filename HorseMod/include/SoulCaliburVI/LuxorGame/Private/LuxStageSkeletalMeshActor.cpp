#include "LuxStageSkeletalMeshActor.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=SceneComponent -FallbackName=SceneComponent
#include "LuxRootMotionComponent.h"
#include "LuxSkeletalMeshComponent.h"

ALuxStageSkeletalMeshActor::ALuxStageSkeletalMeshActor(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("CustomRoot0"));
    this->LuxSkeletalMeshComponent = CreateDefaultSubobject<ULuxSkeletalMeshComponent>(TEXT("LuxSkeletalMeshComponent0"));
    this->LuxRootMotionComponent = CreateDefaultSubobject<ULuxRootMotionComponent>(TEXT("LuxRootMotionComponent0"));
    this->TimeDilation = 1.00f;
    this->LuxSkeletalMeshComponent->SetupAttachment(RootComponent);
}



