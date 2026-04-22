#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=UMGUtil -ObjectName=UIObject -FallbackName=UIObject
#include "LuxUIGameStatusIconHandle.generated.h"

UCLASS(Blueprintable)
class ULuxUIGameStatusIconHandle : public UUIObject {
    GENERATED_BODY()
public:
    ULuxUIGameStatusIconHandle();

    UFUNCTION(BlueprintCallable)
    void Remove();
    
};

