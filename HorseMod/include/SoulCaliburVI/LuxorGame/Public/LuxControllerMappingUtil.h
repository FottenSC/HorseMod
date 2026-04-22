#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=BlueprintFunctionLibrary -FallbackName=BlueprintFunctionLibrary
#include "LuxControllerMappingUtil.generated.h"

UCLASS(Blueprintable)
class ULuxControllerMappingUtil : public UBlueprintFunctionLibrary {
    GENERATED_BODY()
public:
    ULuxControllerMappingUtil();

    UFUNCTION(BlueprintCallable)
    static int32 GetPlayerID(int32 pad_id);
    
    UFUNCTION(BlueprintCallable)
    static int32 GetControllerID(int32 player_side);
    
};

