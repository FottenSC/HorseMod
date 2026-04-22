#pragma once
#include "CoreMinimal.h"
#include "EMaterialSamplerType.generated.h"

UENUM(BlueprintType)
enum EMaterialSamplerType {
    SAMPLERTYPE_Color,
    SAMPLERTYPE_Grayscale,
    SAMPLERTYPE_Alpha,
    SAMPLERTYPE_Normal,
    SAMPLERTYPE_Masks,
    SAMPLERTYPE_DistanceFieldFont,
    SAMPLERTYPE_LinearColor,
    SAMPLERTYPE_LinearGrayscale,
    SAMPLERTYPE_External,
};

