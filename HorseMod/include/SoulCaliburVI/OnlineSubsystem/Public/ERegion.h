#pragma once
#include "CoreMinimal.h"
#include "ERegion.generated.h"

UENUM(BlueprintType)
enum class ERegion : uint8 {
    Asia,
    Africa,
    Europa,
    NorseAmerica,
    MiddleEast,
    Oceania,
    SouthAmerica,
    Other,
    Count,
    World = 255,
};

