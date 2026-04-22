#pragma once
#include "CoreMinimal.h"
#include "ComponentCollisionSettingsChangedSignatureDelegate.generated.h"

class UPrimitiveComponent;

UDELEGATE(BlueprintCallable) DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FComponentCollisionSettingsChangedSignature, UPrimitiveComponent*, ChangedComponent);

