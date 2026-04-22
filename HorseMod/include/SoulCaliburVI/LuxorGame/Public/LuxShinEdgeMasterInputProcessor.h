#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
#include "LuxShinEdgeMasterInputProcessor.generated.h"

UCLASS(Blueprintable)
class LUXORGAME_API ULuxShinEdgeMasterInputProcessor : public UObject {
    GENERATED_BODY()
public:
    ULuxShinEdgeMasterInputProcessor();

    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float GetLstickYaxisMultipleValue();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float GetLstickXaxisMultipleValue();
    
    UFUNCTION(BlueprintCallable)
    static void EnableInputProcessor();
    
    UFUNCTION(BlueprintCallable)
    static void DisableInputProcessor();
    
};

