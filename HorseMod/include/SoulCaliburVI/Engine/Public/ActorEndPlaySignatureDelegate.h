#pragma once
#include "CoreMinimal.h"
#include "EEndPlayReason.h"
#include "ActorEndPlaySignatureDelegate.generated.h"

class AActor;

UDELEGATE(BlueprintCallable) DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FActorEndPlaySignature, AActor*, Actor, TEnumAsByte<EEndPlayReason::Type>, EndPlayReason);

