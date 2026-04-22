#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=BlueprintFunctionLibrary -FallbackName=BlueprintFunctionLibrary
#include "LuxInputUtil.generated.h"

UCLASS(Blueprintable)
class LUXORGAME_API ULuxInputUtil : public UBlueprintFunctionLibrary {
    GENERATED_BODY()
public:
    ULuxInputUtil();

    UFUNCTION(BlueprintCallable)
    static void SetMainUserID(int32 user_id);
    
    UFUNCTION(BlueprintCallable)
    static void ResetMainUser();
    
    UFUNCTION(BlueprintCallable)
    static int32 GetMainUserID();
    
    UFUNCTION(BlueprintCallable)
    static void EmulateTitleDecide();
    
    UFUNCTION(BlueprintCallable)
    static void EmulateRandomBattleInput();
    
    UFUNCTION(BlueprintCallable)
    static void EmulateBattleInput(int32 battle_key, int32 side);
    
};

