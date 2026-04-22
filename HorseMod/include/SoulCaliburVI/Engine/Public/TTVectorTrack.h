#pragma once
#include "CoreMinimal.h"
#include "TTTrackBase.h"
#include "TTVectorTrack.generated.h"

class UCurveVector;

USTRUCT(BlueprintType)
struct FTTVectorTrack : public FTTTrackBase {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    UCurveVector* CurveVector;
    
    ENGINE_API FTTVectorTrack();
};

