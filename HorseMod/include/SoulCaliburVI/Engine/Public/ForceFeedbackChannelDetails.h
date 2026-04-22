#pragma once
#include "CoreMinimal.h"
#include "RuntimeFloatCurve.h"
#include "ForceFeedbackChannelDetails.generated.h"

USTRUCT(BlueprintType)
struct FForceFeedbackChannelDetails {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bAffectsLeftLarge: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bAffectsLeftSmall: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bAffectsRightLarge: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bAffectsRightSmall: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FRuntimeFloatCurve Curve;
    
    ENGINE_API FForceFeedbackChannelDetails();
};

