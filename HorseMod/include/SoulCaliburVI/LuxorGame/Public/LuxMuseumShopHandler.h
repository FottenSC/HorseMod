#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
#include "LuxMuseumShopHandler.generated.h"

UCLASS(Blueprintable)
class ULuxMuseumShopHandler : public UObject {
    GENERATED_BODY()
public:
    ULuxMuseumShopHandler();

    UFUNCTION(BlueprintCallable)
    void OnPurchase(bool bSuccess);
    
};

