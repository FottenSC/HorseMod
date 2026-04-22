#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=UMGUtil -ObjectName=UIMenuWidget -FallbackName=UIMenuWidget
#include "ELuxMenuSoundEventType.h"
#include "LuxDialogMenuWidget.generated.h"

class UUIWindowElement;

UCLASS(Abstract, Blueprintable, EditInlineNew)
class ULuxDialogMenuWidget : public UUIMenuWidget {
    GENERATED_BODY()
public:
    ULuxDialogMenuWidget();

    UFUNCTION(BlueprintCallable)
    UUIWindowElement* openDialogWithTextArguments(const FString& ID, TArray<FString> textArgs, ELuxMenuSoundEventType decisionSeTrigger);
    
    UFUNCTION(BlueprintCallable)
    UUIWindowElement* openDialog(const FString& ID, ELuxMenuSoundEventType decisionSeTrigger);
    
    UFUNCTION(BlueprintCallable)
    void closeDialog();
    
};

