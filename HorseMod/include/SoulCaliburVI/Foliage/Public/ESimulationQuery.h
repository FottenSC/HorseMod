#pragma once
#include "CoreMinimal.h"
#include "ESimulationQuery.generated.h"

UENUM(BlueprintType)
namespace ESimulationQuery {
    enum Type {
        CollisionOverlap = 1,
        ShadeOverlap,
        AnyOverlap,
    };
}

