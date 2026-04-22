#pragma once
#include "CoreMinimal.h"
#include "EDesignPreviewSizeMode.generated.h"

UENUM(BlueprintType)
enum class EDesignPreviewSizeMode : uint8 {
    FillScreen,
    Custom,
    CustomOnScreen,
    Desired,
    DesiredOnScreen,
};

