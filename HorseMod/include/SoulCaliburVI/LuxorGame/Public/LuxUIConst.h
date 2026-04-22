#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=BlueprintFunctionLibrary -FallbackName=BlueprintFunctionLibrary
#include "LuxUIConstData.h"
#include "LuxUIConst.generated.h"

UCLASS(Blueprintable)
class LUXORGAME_API ULuxUIConst : public UBlueprintFunctionLibrary {
    GENERATED_BODY()
public:
    ULuxUIConst();

    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FLuxUIConstData Get();
    
};

