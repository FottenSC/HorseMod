#pragma once
#include "CoreMinimal.h"
#include "EMaterialExposedViewProperty.generated.h"

UENUM(BlueprintType)
enum EMaterialExposedViewProperty {
    MEVP_BufferSize,
    MEVP_FieldOfView,
    MEVP_TanHalfFieldOfView,
    MEVP_ViewSize,
    MEVP_WorldSpaceViewPosition,
    MEVP_WorldSpaceCameraPosition,
    MEVP_ViewportOffset,
};

