#pragma once
#include "CoreMinimal.h"
#include "CaptureResolution.generated.h"

USTRUCT(BlueprintType)
struct MOVIESCENECAPTURE_API FCaptureResolution {
    GENERATED_BODY()
public:
    UPROPERTY(Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 ResX;
    
    UPROPERTY(Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 ResY;
    
    FCaptureResolution();
};

