#include "LuxCharaAppxActor.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=SkeletalMeshComponent -FallbackName=SkeletalMeshComponent

ALuxCharaAppxActor::ALuxCharaAppxActor(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->AppxMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("AppxMeshComponent"));
    this->TargetBoneID = 0;
    this->TargetModeID = 0;
    this->TargetModeValue = 0;
    this->LastHeaderFrame = 0.00f;
    this->TargetHeaderPhase = 0;
}



