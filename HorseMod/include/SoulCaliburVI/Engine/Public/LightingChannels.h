#pragma once
#include "CoreMinimal.h"
#include "LightingChannels.generated.h"

USTRUCT(BlueprintType)
struct FLightingChannels {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bChannel0;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bChannel1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bChannel2;
    
    ENGINE_API FLightingChannels();
};

