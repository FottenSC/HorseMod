#pragma once
#include "CoreMinimal.h"
#include "ETraceScaleType.h"
#include "LuxTracePartsSetting.generated.h"

class UCurveFloat;
class ULuxTraceKindDataAssetList;

USTRUCT(BlueprintType)
struct LUXORGAME_API FLuxTracePartsSetting {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    ETraceScaleType ScaleType;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float Width;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    UCurveFloat* FrameScale;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    ULuxTraceKindDataAssetList* KindDataAssetList;
    
    FLuxTracePartsSetting();
};

