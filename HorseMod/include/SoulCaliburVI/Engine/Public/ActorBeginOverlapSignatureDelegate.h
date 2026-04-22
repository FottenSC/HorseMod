#pragma once
#include "CoreMinimal.h"
#include "ActorBeginOverlapSignatureDelegate.generated.h"

class AActor;

UDELEGATE(BlueprintCallable) DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FActorBeginOverlapSignature, AActor*, OverlappedActor, AActor*, OtherActor);

