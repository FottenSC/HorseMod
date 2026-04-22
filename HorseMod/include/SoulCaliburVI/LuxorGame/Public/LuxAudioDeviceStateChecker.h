#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
#include "LuxAudioDeviceStateChecker.generated.h"

UCLASS(Blueprintable)
class LUXORGAME_API ULuxAudioDeviceStateChecker : public UObject {
    GENERATED_BODY()
public:
    ULuxAudioDeviceStateChecker();

    UFUNCTION(BlueprintCallable)
    static bool HasValidAudioDevice();
    
};

