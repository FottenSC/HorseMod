#pragma once
#include "CoreMinimal.h"
#include "ComponentEndCursorOverSignatureDelegate.generated.h"

class UPrimitiveComponent;

UDELEGATE(BlueprintCallable) DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FComponentEndCursorOverSignature, UPrimitiveComponent*, TouchedComponent);

