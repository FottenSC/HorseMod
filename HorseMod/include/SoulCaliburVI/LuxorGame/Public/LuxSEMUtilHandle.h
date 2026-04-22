#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
#include "LuxSEMUtilHandle.generated.h"

UCLASS(Blueprintable)
class ULuxSEMUtilHandle : public UObject {
    GENERATED_BODY()
public:
    ULuxSEMUtilHandle();

    UFUNCTION(BlueprintCallable)
    void Remove();
    
};

