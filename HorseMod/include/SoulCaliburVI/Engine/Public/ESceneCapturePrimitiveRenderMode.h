#pragma once
#include "CoreMinimal.h"
#include "ESceneCapturePrimitiveRenderMode.generated.h"

UENUM(BlueprintType)
enum ESceneCapturePrimitiveRenderMode {
    PRM_LegacySceneCapture,
    PRM_RenderScenePrimitives,
    PRM_UseShowOnlyList,
};

