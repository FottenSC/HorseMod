#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=LinearColor -FallbackName=LinearColor
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=Actor -FallbackName=Actor
#include "CamAwareTargetParam.h"
#include "DMCamOccluder.generated.h"

UCLASS(Abstract, Blueprintable)
class ADMCamOccluder : public AActor {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bDebugAppeal;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FCamAwareTargetParam> Targets;
    
    ADMCamOccluder(const FObjectInitializer& ObjectInitializer);

protected:
    UFUNCTION(BlueprintCallable)
    void SetVectorParam(FName ParamName, FLinearColor ParamValue);
    
    UFUNCTION(BlueprintCallable)
    void SetScalarParam(FName ParamName, float ParamValue);
    
    UFUNCTION(BlueprintCallable)
    void Init();
    
};

