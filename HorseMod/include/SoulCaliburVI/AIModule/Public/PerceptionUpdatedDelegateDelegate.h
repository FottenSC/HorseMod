#pragma once
#include "CoreMinimal.h"
#include "PerceptionUpdatedDelegateDelegate.generated.h"

class AActor;

UDELEGATE(BlueprintCallable) DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPerceptionUpdatedDelegate, TArray<AActor*>, UpdatedActors);

