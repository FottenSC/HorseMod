#pragma once
#include "CoreMinimal.h"
#include "ELargeRegion.generated.h"

UENUM(BlueprintType)
enum class ELargeRegion : uint8 {
    Asia,
    America,
    Europa,
    Other,
    Count,
    World = 255,
};

