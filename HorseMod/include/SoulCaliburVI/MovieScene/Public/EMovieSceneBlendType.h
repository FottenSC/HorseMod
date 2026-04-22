#pragma once
#include "CoreMinimal.h"
#include "EMovieSceneBlendType.generated.h"

UENUM(BlueprintType)
enum class EMovieSceneBlendType : uint8 {
    Absolute = 1,
    Additive,
    Relative = 4,
};

