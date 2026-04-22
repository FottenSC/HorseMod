#pragma once
#include "CoreMinimal.h"
#include "TTTrackBase.h"
#include "TTFloatTrack.generated.h"

class UCurveFloat;

USTRUCT(BlueprintType)
struct FTTFloatTrack : public FTTTrackBase {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    UCurveFloat* CurveFloat;
    
    ENGINE_API FTTFloatTrack();
};

