#pragma once
#include "CoreMinimal.h"
#include "CaptureProtocolID.generated.h"

USTRUCT(BlueprintType)
struct FCaptureProtocolID {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FName Identifier;
    
    MOVIESCENECAPTURE_API FCaptureProtocolID();
};

