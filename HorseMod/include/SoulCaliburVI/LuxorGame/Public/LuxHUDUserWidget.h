#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=UMG -ObjectName=ESlateVisibility -FallbackName=ESlateVisibility
//CROSS-MODULE INCLUDE V2: -ModuleName=UMG -ObjectName=UserWidget -FallbackName=UserWidget
#include "LuxHUDUserWidget.generated.h"

UCLASS(Blueprintable, EditInlineNew)
class ULuxHUDUserWidget : public UUserWidget {
    GENERATED_BODY()
public:
    ULuxHUDUserWidget();

protected:
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void OnChangeVisibility(ESlateVisibility InVisibility);
    
};

