#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=BlueprintFunctionLibrary -FallbackName=BlueprintFunctionLibrary
#include "LuxUIShopFlowParam.h"
#include "LuxUIShopFlowUtil.generated.h"

UCLASS(Blueprintable)
class LUXORGAME_API ULuxUIShopFlowUtil : public UBlueprintFunctionLibrary {
    GENERATED_BODY()
public:
    ULuxUIShopFlowUtil();

    UFUNCTION(BlueprintCallable)
    static void Purchase(FLuxUIShopFlowParam InParam);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool IsShopItem(const FString& InContentId);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static TArray<FString> GetShopContentIDs();
    
};

