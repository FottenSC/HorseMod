#pragma once
#include "CoreMinimal.h"
#include "SoundSubmixSendInfo.generated.h"

class USoundSubmix;

USTRUCT(BlueprintType)
struct ENGINE_API FSoundSubmixSendInfo {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float SendLevel;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    USoundSubmix* SoundSubmix;
    
    FSoundSubmixSendInfo();
};

