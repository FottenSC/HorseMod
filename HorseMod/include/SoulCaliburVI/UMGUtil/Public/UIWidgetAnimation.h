#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
#include "UIWidgetAnimationPlayParam.h"
#include "UIWidgetAnimation.generated.h"

class UBaseUserWidget;
class UWidgetAnimation;

UCLASS(Blueprintable)
class UUIWidgetAnimation : public UObject {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    UWidgetAnimation* WidgetAnimation;
    
    UUIWidgetAnimation();

    UFUNCTION(BlueprintCallable)
    void Stop(UBaseUserWidget* inWidget);
    
    UFUNCTION(BlueprintCallable)
    void Play(UBaseUserWidget* inWidget, const FUIWidgetAnimationPlayParam& InParam);
    
    UFUNCTION(BlueprintCallable)
    void OnAnimationFinishedDelegate();
    
};

