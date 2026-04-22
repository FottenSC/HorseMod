#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
#include "LuxUIPadAssignmentHandler.generated.h"

UCLASS(Blueprintable)
class ULuxUIPadAssignmentHandler : public UObject {
    GENERATED_BODY()
public:
    ULuxUIPadAssignmentHandler();

    UFUNCTION(BlueprintCallable)
    void OnComplete(bool bSuccess, int32 ControllerId);
    
};

