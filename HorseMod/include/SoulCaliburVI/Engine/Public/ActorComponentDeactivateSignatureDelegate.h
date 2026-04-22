#pragma once
#include "CoreMinimal.h"
#include "ActorComponentDeactivateSignatureDelegate.generated.h"

class UActorComponent;

UDELEGATE(BlueprintCallable) DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FActorComponentDeactivateSignature, UActorComponent*, Component);

