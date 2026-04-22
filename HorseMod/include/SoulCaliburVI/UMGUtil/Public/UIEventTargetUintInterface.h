#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Interface -FallbackName=Interface
#include "UIEventTargetUintInterface.generated.h"

class UUIEventTargetUnitObject;

UINTERFACE(Blueprintable)
class UMGUTIL_API UUIEventTargetUintInterface : public UInterface {
    GENERATED_BODY()
};

class UMGUTIL_API IUIEventTargetUintInterface : public IInterface {
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    UUIEventTargetUnitObject* setEventTargetUnitObject(UUIEventTargetUnitObject* unit);
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    UUIEventTargetUnitObject* getEventTargetUnitObject() const;
    
};

