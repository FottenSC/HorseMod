#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
//CROSS-MODULE INCLUDE V2: -ModuleName=InputCore -ObjectName=Key -FallbackName=Key
#include "ELuxBattleCombinationKey.h"
#include "ELuxBattleInputDevice.h"
#include "ELuxBattleInputType.h"
#include "ELuxBattleKey.h"
#include "LuxBattleCombinationKeyData.h"
#include "LuxBattleInput.h"
#include "LuxBattleInputPairs.h"
#include "LuxBattleInputKeyConfig.generated.h"

class ULuxBattleInputBinding;

UCLASS(Blueprintable)
class LUXORGAME_API ULuxBattleInputKeyConfig : public UObject {
    GENERATED_BODY()
public:
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    TMap<int32, FLuxBattleInputPairs> InputPairsMap;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    TMap<int32, ULuxBattleInputBinding*> DefaultPresetsMap;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    TArray<FLuxBattleCombinationKeyData> CombinationKeyData;
    
public:
    ULuxBattleInputKeyConfig();

    UFUNCTION(BlueprintCallable)
    static void SetBindings(const FLuxBattleInputPairs& Bindings, ELuxBattleInputDevice inDevice, ELuxBattleInputType inType, bool isSecondaryKey);
    
    UFUNCTION(BlueprintCallable)
    static FString SetBindingInput(ELuxBattleInputDevice inDevice, ELuxBattleCombinationKey inCombinationKey, const FKey& inPhysKey, ELuxBattleInputType inType, bool isSecondaryKey);
    
    UFUNCTION(BlueprintCallable)
    static void ResetBindings(ELuxBattleInputDevice inDevice, ELuxBattleInputType inType, bool isSecondaryKey);
    
    UFUNCTION(BlueprintCallable)
    static void ResetAllBindings();
    
    UFUNCTION(BlueprintCallable)
    static FString GetPhysKeyName(ELuxBattleInputDevice device, ELuxBattleCombinationKey combinationKey, ELuxBattleInputType Type, bool isSecondaryKey);
    
    UFUNCTION(BlueprintCallable)
    static TArray<ELuxBattleCombinationKey> GetCombinationKeys(bool bKeyboardDevice);
    
    UFUNCTION(BlueprintCallable)
    static FString GetCombinationKeyLabel(ELuxBattleCombinationKey combinationKey);
    
    UFUNCTION(BlueprintCallable)
    static FLuxBattleInputPairs GetBindings(ELuxBattleInputDevice inDevice, ELuxBattleInputType inType, bool isSecondaryKey);
    
    UFUNCTION(BlueprintCallable)
    static FString GetBindingInputState(ELuxBattleInputDevice inDevice, ELuxBattleCombinationKey inKeyCombination, ELuxBattleInputType inType, bool isSecondaryKey);
    
    UFUNCTION(BlueprintCallable)
    static FLuxBattleInput GetBindingInput(ELuxBattleInputDevice inDevice, ELuxBattleCombinationKey inKeyCombination, ELuxBattleInputType inType, bool isSecondaryKey);
    
    UFUNCTION(BlueprintCallable)
    static TArray<ELuxBattleKey> ConvertPhysKeyToBattleKey(ELuxBattleInputDevice inDevice, const FKey& inPhysKey, ELuxBattleInputType inType, bool isSecondaryKey);
    
    UFUNCTION(BlueprintCallable)
    static void ApplyToInputProcessor();
    
};

