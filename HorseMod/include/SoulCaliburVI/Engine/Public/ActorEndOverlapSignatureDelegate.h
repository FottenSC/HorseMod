#pragma once
#include "CoreMinimal.h"
#include "ActorEndOverlapSignatureDelegate.generated.h"

class AActor;

UDELEGATE(BlueprintCallable) DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FActorEndOverlapSignature, AActor*, OverlappedActor, AActor*, OtherActor);

