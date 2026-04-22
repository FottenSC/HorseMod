#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=SkeletalMeshComponent -FallbackName=SkeletalMeshComponent
#include "LuxSkeletalMeshComponent.generated.h"

UCLASS(Blueprintable, EditInlineNew, ClassGroup=Custom, meta=(BlueprintSpawnableComponent))
class LUXORGAME_API ULuxSkeletalMeshComponent : public USkeletalMeshComponent {
    GENERATED_BODY()
public:
    ULuxSkeletalMeshComponent(const FObjectInitializer& ObjectInitializer);

};

