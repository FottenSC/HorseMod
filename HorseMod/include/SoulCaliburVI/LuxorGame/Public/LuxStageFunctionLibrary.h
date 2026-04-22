#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=BlueprintFunctionLibrary -FallbackName=BlueprintFunctionLibrary
#include "LuxStageFunctionLibrary.generated.h"

class UObject;

UCLASS(Blueprintable)
class LUXORGAME_API ULuxStageFunctionLibrary : public UBlueprintFunctionLibrary {
    GENERATED_BODY()
public:
    ULuxStageFunctionLibrary();

    UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContext"))
    static void BroadcastMessageToLevelScript(UObject* WorldContext, const FString& Message);
    
};

