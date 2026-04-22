#pragma once
#include "CoreMinimal.h"
#include "TTTrackBase.generated.h"

USTRUCT(BlueprintType)
struct FTTTrackBase {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FName TrackName;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bIsExternalCurve;
    
    ENGINE_API FTTTrackBase();
};

