#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
//CROSS-MODULE INCLUDE V2: -ModuleName=SlateCore -ObjectName=ETextCommit -FallbackName=ETextCommit
#include "LuxWindowsVirtualKeyboardObject.generated.h"

class UEditableText;
class UUserWidget;

UCLASS(Blueprintable)
class ULuxWindowsVirtualKeyboardObject : public UObject {
    GENERATED_BODY()
public:
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    UUserWidget* KeyboardWidget;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    UEditableText* InputText;
    
public:
    ULuxWindowsVirtualKeyboardObject();

private:
    UFUNCTION(BlueprintCallable)
    void OnTextChangedHandler(const FText& Text);
    
    UFUNCTION(BlueprintCallable)
    void OnCompletionHandler(const FText& Text, TEnumAsByte<ETextCommit::Type> CommitMethod);
    
};

