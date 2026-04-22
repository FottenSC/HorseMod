#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=InputCore -ObjectName=Key -FallbackName=Key
#include "UIObject.h"
#include "KeyContainer.generated.h"

UCLASS(Blueprintable)
class UMGUTIL_API UKeyContainer : public UUIObject {
    GENERATED_BODY()
public:
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FKey Key;
    
public:
    UKeyContainer();

    UFUNCTION(BlueprintCallable)
    void SetFKey(FKey InKey);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FKey GetFKey();
    
};

