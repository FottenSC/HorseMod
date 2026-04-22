#pragma once
#include "CoreMinimal.h"
#include "CullDistanceSizePair.h"
#include "Volume.h"
#include "CullDistanceVolume.generated.h"

UCLASS(Blueprintable)
class ACullDistanceVolume : public AVolume {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FCullDistanceSizePair> CullDistances;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bEnabled: 1;
    
    ACullDistanceVolume(const FObjectInitializer& ObjectInitializer);

};

