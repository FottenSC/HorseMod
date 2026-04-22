#pragma once
#include "CoreMinimal.h"
#include "ENiagaraSortMode.generated.h"

UENUM(BlueprintType)
enum class ENiagaraSortMode : uint8 {
    SortNone,
    SortViewDepth,
    SortViewDistance,
};

