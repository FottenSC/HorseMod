#pragma once
#include "CoreMinimal.h"
#include "OnNavDataGenerigEventDelegate.generated.h"

class ANavigationData;

UDELEGATE(BlueprintCallable) DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNavDataGenerigEvent, ANavigationData*, NavData);

