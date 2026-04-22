#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=UMGUtil -ObjectName=UIObject -FallbackName=UIObject
#include "LuxUIGameStatusIconInstance.generated.h"

class UBaseUserWidget;
class UObject;
class UWidgetAnimation;
class UWorld;

UCLASS(Blueprintable)
class ULuxUIGameStatusIconInstance : public UUIObject {
    GENERATED_BODY()
public:
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, Transient, meta=(AllowPrivateAccess=true))
    UBaseUserWidget* IconWidget;
    
public:
    ULuxUIGameStatusIconInstance();

private:
    UFUNCTION(BlueprintCallable)
    void OnPreLoadMap(const FString& InLoadMapName);
    
    UFUNCTION(BlueprintCallable)
    void OnPostLoadMap(UWorld* inWorld);
    
    UFUNCTION(BlueprintCallable)
    void OnAnimationFinished_fadeout(UObject* InObject, UWidgetAnimation* InWidgetAnimation);
    
    UFUNCTION(BlueprintCallable)
    void OnAnimationFinished_fadein(UObject* InObject, UWidgetAnimation* InWidgetAnimation);
    
};

