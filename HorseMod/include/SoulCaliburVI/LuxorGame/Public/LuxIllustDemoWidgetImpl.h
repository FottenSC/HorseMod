#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=UMGUtil -ObjectName=UIWidgetImpl -FallbackName=UIWidgetImpl
#include "LuxIllustDemoWidgetImpl.generated.h"

UCLASS(Blueprintable)
class ULuxIllustDemoWidgetImpl : public UUIWidgetImpl {
    GENERATED_BODY()
public:
    ULuxIllustDemoWidgetImpl();

    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void AddToLog(const FText& Speaker, const FText& Message);
    
};

