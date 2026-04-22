#pragma once
#include "CoreMinimal.h"
#include "ExposureSettings.generated.h"

USTRUCT(BlueprintType)
struct FExposureSettings {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    int32 LogOffset;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bFixed;
    
    ENGINE_API FExposureSettings();
};

