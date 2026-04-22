#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
#include "LuxGameTerminator.generated.h"

UCLASS(Blueprintable)
class LUXORGAME_API ULuxGameTerminator : public UObject {
    GENERATED_BODY()
public:
    ULuxGameTerminator();

    UFUNCTION(BlueprintCallable)
    static void RequestTermination();
    
};

