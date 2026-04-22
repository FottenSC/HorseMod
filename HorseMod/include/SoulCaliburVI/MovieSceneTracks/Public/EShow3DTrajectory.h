#pragma once
#include "CoreMinimal.h"
#include "EShow3DTrajectory.generated.h"

UENUM(BlueprintType)
enum class EShow3DTrajectory : uint8 {
    EST_OnlyWhenSelected,
    EST_Always,
    EST_Never,
};

