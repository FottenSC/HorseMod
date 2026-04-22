#pragma once
#include "CoreMinimal.h"
#include "ClothVertBoneData.generated.h"

USTRUCT(BlueprintType)
struct FClothVertBoneData {
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, meta=(AllowPrivateAccess=true))
    uint16 BoneIndices[8];
    
    UPROPERTY(EditAnywhere, meta=(AllowPrivateAccess=true))
    float BoneWeights[8];
    
    CLOTHINGSYSTEMRUNTIME_API FClothVertBoneData();
};

