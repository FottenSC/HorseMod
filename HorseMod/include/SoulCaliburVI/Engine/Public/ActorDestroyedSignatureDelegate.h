#pragma once
#include "CoreMinimal.h"
#include "ActorDestroyedSignatureDelegate.generated.h"

class AActor;

UDELEGATE(BlueprintCallable) DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FActorDestroyedSignature, AActor*, DestroyedActor);

