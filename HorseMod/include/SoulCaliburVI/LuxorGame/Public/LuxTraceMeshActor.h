#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=SkeletalMeshActor -FallbackName=SkeletalMeshActor
#include "LuxTraceMeshActor.generated.h"

UCLASS(Blueprintable)
class ALuxTraceMeshActor : public ASkeletalMeshActor {
    GENERATED_BODY()
public:
    ALuxTraceMeshActor(const FObjectInitializer& ObjectInitializer);

};

