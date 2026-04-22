#pragma once
#include "CoreMinimal.h"
#include "ELuxColorEditableFlag.generated.h"

UENUM(BlueprintType)
enum class ELuxColorEditableFlag : uint8 {
    ECE_FLAG_None,
    ECE_FLAG_Color,
    ECE_FLAG_Tiling = 3,
    ECE_FLAG_Common = 7,
    ENUM_MAX UMETA(Hidden),
};

