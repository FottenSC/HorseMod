#pragma once
#include "CoreMinimal.h"
#include "LandscapeWeightmapUsage.generated.h"

class ULandscapeComponent;

USTRUCT(BlueprintType)
struct FLandscapeWeightmapUsage {
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    ULandscapeComponent* ChannelUsage[4];
    
    LANDSCAPE_API FLandscapeWeightmapUsage();
};

