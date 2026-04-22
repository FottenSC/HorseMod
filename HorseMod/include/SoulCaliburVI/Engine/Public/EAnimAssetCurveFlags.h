#pragma once
#include "CoreMinimal.h"
#include "EAnimAssetCurveFlags.generated.h"

UENUM(BlueprintType)
enum EAnimAssetCurveFlags {
    AACF_DriveMorphTarget_DEPRECATED = 1,
    AACF_DriveAttribute_DEPRECATED,
    AACF_Editable = 4,
    AACF_DriveMaterial_DEPRECATED = 8,
    AACF_Metadata = 16,
    AACF_DriveTrack = 32,
    AACF_Disabled = 64,
};

