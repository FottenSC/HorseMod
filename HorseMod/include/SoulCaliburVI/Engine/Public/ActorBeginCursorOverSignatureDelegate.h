#pragma once
#include "CoreMinimal.h"
#include "ActorBeginCursorOverSignatureDelegate.generated.h"

class AActor;

UDELEGATE(BlueprintCallable) DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FActorBeginCursorOverSignature, AActor*, TouchedActor);

