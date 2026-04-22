#pragma once
#include "CoreMinimal.h"
#include "EInputConsumeOptions.generated.h"

UENUM(BlueprintType)
enum EInputConsumeOptions {
    ICO_ConsumeAll,
    ICO_ConsumeBoundKeys,
    ICO_ConsumeNone,
};

