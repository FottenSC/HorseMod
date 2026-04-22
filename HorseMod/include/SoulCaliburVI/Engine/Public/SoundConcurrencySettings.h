#pragma once
#include "CoreMinimal.h"
#include "EMaxConcurrentResolutionRule.h"
#include "SoundConcurrencySettings.generated.h"

USTRUCT(BlueprintType)
struct ENGINE_API FSoundConcurrencySettings {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    int32 MaxCount;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bLimitToOwner: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TEnumAsByte<EMaxConcurrentResolutionRule::Type> ResolutionRule;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float VolumeScale;
    
    FSoundConcurrencySettings();
};

