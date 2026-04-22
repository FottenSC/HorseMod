#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=UMGUtil -ObjectName=UIMenuWidget -FallbackName=UIMenuWidget
#include "LuxTutorialMenuWidget.generated.h"

class UUIWindowElement;

UCLASS(Abstract, Blueprintable, EditInlineNew)
class ULuxTutorialMenuWidget : public UUIMenuWidget {
    GENERATED_BODY()
public:
    ULuxTutorialMenuWidget();

    UFUNCTION(BlueprintCallable)
    UUIWindowElement* openTutorial(const FString& ID);
    
    UFUNCTION(BlueprintCallable)
    void closeTutorial();
    
};

