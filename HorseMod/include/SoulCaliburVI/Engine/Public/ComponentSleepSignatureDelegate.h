#pragma once
#include "CoreMinimal.h"
#include "ComponentSleepSignatureDelegate.generated.h"

class UPrimitiveComponent;

UDELEGATE(BlueprintCallable) DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FComponentSleepSignature, UPrimitiveComponent*, SleepingComponent, FName, BoneName);

