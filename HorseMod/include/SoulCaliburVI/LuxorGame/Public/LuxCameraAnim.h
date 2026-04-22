#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=CameraAnim -FallbackName=CameraAnim
#include "LuxCameraAnim.generated.h"

UCLASS(Blueprintable)
class LUXORGAME_API ULuxCameraAnim : public UCameraAnim {
    GENERATED_BODY()
public:
    ULuxCameraAnim();

};

