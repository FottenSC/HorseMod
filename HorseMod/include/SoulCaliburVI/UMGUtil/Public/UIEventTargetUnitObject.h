#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
#include "UIEventListenerUnit.h"
#include "UIEventTarget.h"
#include "UIEventTargetUnitObject.generated.h"

UCLASS(Blueprintable)
class UUIEventTargetUnitObject : public UObject, public IUIEventTarget {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FUIEventListenerUnit> EventListenerMap;
    
    UUIEventTargetUnitObject();


    // Fix for true pure virtual functions not being implemented
};

