#pragma once
#include "CoreMinimal.h"
#include "EMaterialExpressionScreenPositionMapping.generated.h"

UENUM(BlueprintType)
enum EMaterialExpressionScreenPositionMapping {
    MESP_SceneTextureUV,
    MESP_ViewportUV,
};

