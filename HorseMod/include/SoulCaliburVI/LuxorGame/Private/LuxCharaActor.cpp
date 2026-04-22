#include "LuxCharaActor.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=SceneComponent -FallbackName=SceneComponent
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=SkeletalMeshComponent -FallbackName=SkeletalMeshComponent

ALuxCharaActor::ALuxCharaActor(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("CustomRoot0"));
    this->CharaMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CharaMesh0"));
    this->WeaponMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponMesh0"));
    this->CharaMeshComponent->SetupAttachment(RootComponent);
    this->WeaponMeshComponent->SetupAttachment(RootComponent);
}


