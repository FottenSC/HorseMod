#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
#include "MovieSceneCaptureEnvironment.generated.h"

UCLASS(Blueprintable)
class UMovieSceneCaptureEnvironment : public UObject {
    GENERATED_BODY()
public:
    UMovieSceneCaptureEnvironment();

    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 GetCaptureFrameNumber();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float GetCaptureElapsedTime();
    
};

