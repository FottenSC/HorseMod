#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Interface -FallbackName=Interface
#include "LuxUIAssetHubReceiver.generated.h"

class ULuxUIAssetLoader;

UINTERFACE(Blueprintable)
class ULuxUIAssetHubReceiver : public UInterface {
    GENERATED_BODY()
};

class ILuxUIAssetHubReceiver : public IInterface {
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void OnUIAssetLoadCompleted(ULuxUIAssetLoader* UIAssetLoader);
    
};

