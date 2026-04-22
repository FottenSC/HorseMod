#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
#include "UIEventListenerUnit.h"
#include "UIEventTarget.h"
#include "UIAnimationEventHandler.generated.h"

UCLASS(Blueprintable)
class UUIAnimationEventHandler : public UObject, public IUIEventTarget {
    GENERATED_BODY()
public:
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FUIEventListenerUnit> EventListenerMap;
    
public:
    UUIAnimationEventHandler();

    UFUNCTION(BlueprintCallable)
    void onFinishAnimation();
    

    // Fix for true pure virtual functions not being implemented
};

