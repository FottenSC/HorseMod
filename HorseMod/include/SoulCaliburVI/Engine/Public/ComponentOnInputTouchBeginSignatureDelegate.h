#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=InputCore -ObjectName=ETouchIndex -FallbackName=ETouchIndex
#include "ComponentOnInputTouchBeginSignatureDelegate.generated.h"

class UPrimitiveComponent;

UDELEGATE(BlueprintCallable) DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FComponentOnInputTouchBeginSignature, TEnumAsByte<ETouchIndex::Type>, FingerIndex, UPrimitiveComponent*, TouchedComponent);

