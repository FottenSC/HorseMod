#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=AnimInstance -FallbackName=AnimInstance
#include "AppxMeshAnimationParam.h"
#include "LuxCharaAppxAnimInstance.generated.h"

UCLASS(Blueprintable, NonTransient)
class LUXORGAME_API ULuxCharaAppxAnimInstance : public UAnimInstance {
    GENERATED_BODY()
public:
    ULuxCharaAppxAnimInstance();

    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void SetAnimationPosition(float pos);
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void OnChangeAnimationState(FAppxMeshAnimationParam Param);
    
};

