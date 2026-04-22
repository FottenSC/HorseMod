#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=CameraActor -FallbackName=CameraActor
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=ViewTargetTransitionParams -FallbackName=ViewTargetTransitionParams
#include "LuxCamera.generated.h"

UCLASS(Blueprintable)
class LUXORGAME_API ALuxCamera : public ACameraActor {
    GENERATED_BODY()
public:
    ALuxCamera(const FObjectInitializer& ObjectInitializer);

    UFUNCTION(BlueprintCallable)
    void SetViewTargetForPlayer(int32 PlayerIndex, const FViewTargetTransitionParams& TransitionParams);
    
    UFUNCTION(BlueprintCallable)
    void SetViewTarget(const FViewTargetTransitionParams& TransitionParams);
    
};

