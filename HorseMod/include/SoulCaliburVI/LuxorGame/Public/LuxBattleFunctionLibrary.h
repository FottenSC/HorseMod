#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=BlueprintFunctionLibrary -FallbackName=BlueprintFunctionLibrary
#include "ELuxBattlePauseType.h"
#include "LuxBattleFunctionLibrary.generated.h"

class ALuxBattleGameMode;
class ALuxBattleManager;
class ALuxBattlePauseController;
class UObject;

UCLASS(Blueprintable)
class LUXORGAME_API ULuxBattleFunctionLibrary : public UBlueprintFunctionLibrary {
    GENERATED_BODY()
public:
    ULuxBattleFunctionLibrary();

    UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContext"))
    static void StepInBattlePause(UObject* WorldContext);
    
    UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContext"))
    static void SetUserInputCheck(UObject* WorldContext, bool bEnabled);
    
    UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContext"))
    static void SetSoulGaugeInfinity(UObject* WorldContext, int32 inPlayerIndex, bool bInfinite);
    
    UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContext"))
    static void SetImmortality(UObject* WorldContext, int32 inPlayerIndex, bool bImmortal);
    
    UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContext"))
    static void SetBattlePause(UObject* WorldContext, ELuxBattlePauseType inType, bool bPause);
    
    UFUNCTION(BlueprintCallable, BlueprintPure, meta=(WorldContext="WorldContext"))
    static bool IsMatchFinished(UObject* WorldContext);
    
    UFUNCTION(BlueprintCallable, BlueprintPure, meta=(WorldContext="WorldContext"))
    static bool IsLocalUserControl(UObject* WorldContext, int32 PlayerIndex);
    
    UFUNCTION(BlueprintCallable, BlueprintPure, meta=(WorldContext="WorldContext"))
    static bool IsFinishBlow(UObject* WorldContext);
    
    UFUNCTION(BlueprintCallable, BlueprintPure, meta=(WorldContext="WorldContext"))
    static bool IsBattlePlaying(UObject* WorldContext);
    
    UFUNCTION(BlueprintCallable, BlueprintPure, meta=(WorldContext="WorldContext"))
    static bool IsBattlePaused(UObject* WorldContext, ELuxBattlePauseType inType);
    
    UFUNCTION(BlueprintCallable, BlueprintPure, meta=(WorldContext="WorldContext"))
    static bool IsBattleOnlineInputSync(UObject* WorldContext);
    
    UFUNCTION(BlueprintCallable, BlueprintPure, meta=(WorldContext="WorldContext"))
    static bool IsBattleOnline(UObject* WorldContext);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static void GetBuildConfiguration(FString& Configuration);
    
    UFUNCTION(BlueprintCallable, BlueprintPure, meta=(WorldContext="WorldContext"))
    static ALuxBattlePauseController* GetBattlePauseController(UObject* WorldContext);
    
    UFUNCTION(BlueprintCallable, BlueprintPure, meta=(WorldContext="WorldContext"))
    static ALuxBattleManager* GetBattleManager(UObject* WorldContext);
    
    UFUNCTION(BlueprintCallable, BlueprintPure, meta=(WorldContext="WorldContext"))
    static ALuxBattleGameMode* GetBattleGameMode(UObject* WorldContext);
    
    UFUNCTION(BlueprintCallable, BlueprintPure, meta=(WorldContext="WorldContext"))
    static bool BattlePauseEnabled(UObject* WorldContext);
    
};

