#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=BlueprintFunctionLibrary -FallbackName=BlueprintFunctionLibrary
#include "ELuxMenuSoundEventType.h"
#include "LuxUIMenuDialogUtil.generated.h"

class UUIMenuWidget;
class UUIWindowElement;

UCLASS(Blueprintable)
class LUXORGAME_API ULuxUIMenuDialogUtil : public UBlueprintFunctionLibrary {
    GENERATED_BODY()
public:
    ULuxUIMenuDialogUtil();

    UFUNCTION(BlueprintCallable)
    static UUIWindowElement* CreateWaitDialog_DEPRECATED(UUIMenuWidget* UIMenu, UUIWindowElement* ResponsibleWindow, const FString& DialogId);
    
    UFUNCTION(BlueprintCallable)
    static UUIWindowElement* CreateWaitCancelDialog_DEPRECATED(UUIMenuWidget* UIMenu, UUIWindowElement* ResponsibleWindow, const FString& DialogId);
    
    UFUNCTION(BlueprintCallable)
    static UUIWindowElement* CreateQuestionDialog_DEPRECATED(UUIMenuWidget* UIMenu, UUIWindowElement* ResponsibleWindow, const FString& DialogId);
    
    UFUNCTION(BlueprintCallable)
    static UUIWindowElement* CreateInfoDialog_DEPRECATED(UUIMenuWidget* UIMenu, UUIWindowElement* ResponsibleWindow, const FString& DialogId);
    
    UFUNCTION(BlueprintCallable)
    static UUIWindowElement* CreateErrorDialog_DEPRECATED(UUIMenuWidget* UIMenu, UUIWindowElement* ResponsibleWindow, const FString& DialogId);
    
    UFUNCTION(BlueprintCallable)
    static UUIWindowElement* CreateDialogMenu(UUIMenuWidget* UIMenu, UUIWindowElement* ResponsibleWindow, const FString& DialogId, ELuxMenuSoundEventType decisionSeTrigger);
    
};

