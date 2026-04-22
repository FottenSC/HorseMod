#pragma once
#include "CoreMinimal.h"
#include "FoliageVertexColorChannelMask.generated.h"

USTRUCT(BlueprintType)
struct FFoliageVertexColorChannelMask {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 UseMask: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float MaskThreshold;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 InvertMask: 1;
    
    FOLIAGE_API FFoliageVertexColorChannelMask();
};

