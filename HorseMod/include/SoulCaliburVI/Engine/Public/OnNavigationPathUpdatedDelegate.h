#pragma once
#include "CoreMinimal.h"
#include "ENavPathEvent.h"
#include "OnNavigationPathUpdatedDelegate.generated.h"

class UNavigationPath;

UDELEGATE(BlueprintCallable) DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnNavigationPathUpdated, UNavigationPath*, AffectedPath, TEnumAsByte<ENavPathEvent::Type>, PathEvent);

