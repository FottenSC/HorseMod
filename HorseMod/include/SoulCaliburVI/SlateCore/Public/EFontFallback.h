#pragma once
#include "CoreMinimal.h"
#include "EFontFallback.generated.h"

UENUM(BlueprintType)
enum class EFontFallback : uint8 {
    FF_NoFallback,
    FF_LocalizedFallback,
    FF_LastResortFallback,
};

