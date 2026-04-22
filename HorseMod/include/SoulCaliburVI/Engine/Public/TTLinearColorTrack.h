#pragma once
#include "CoreMinimal.h"
#include "TTTrackBase.h"
#include "TTLinearColorTrack.generated.h"

class UCurveLinearColor;

USTRUCT(BlueprintType)
struct FTTLinearColorTrack : public FTTTrackBase {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    UCurveLinearColor* CurveLinearColor;
    
    ENGINE_API FTTLinearColorTrack();
};

