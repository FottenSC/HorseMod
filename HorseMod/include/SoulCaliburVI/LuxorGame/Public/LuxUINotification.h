#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=UMGUtil -ObjectName=UIObject -FallbackName=UIObject
#include "LuxUINotification.generated.h"

class UObject;
class UUserWidget;
class UVerticalBox;
class UWidgetAnimation;

UCLASS(Blueprintable)
class LUXORGAME_API ULuxUINotification : public UUIObject {
    GENERATED_BODY()
public:
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    UUserWidget* UINotify;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    UVerticalBox* NotifyItems;
    
public:
    ULuxUINotification();

private:
    UFUNCTION(BlueprintCallable)
    void OnAnimationFinished_wait(UObject* InObject, UWidgetAnimation* InWidgetAnimation);
    
    UFUNCTION(BlueprintCallable)
    void OnAnimationFinished_fadeout(UObject* InObject, UWidgetAnimation* InWidgetAnimation);
    
    UFUNCTION(BlueprintCallable)
    void OnAnimationFinished_fadein(UObject* InObject, UWidgetAnimation* InWidgetAnimation);
    
};

