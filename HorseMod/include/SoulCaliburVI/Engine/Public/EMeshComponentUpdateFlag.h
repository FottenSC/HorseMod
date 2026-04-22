#pragma once
#include "CoreMinimal.h"
#include "EMeshComponentUpdateFlag.generated.h"

UENUM(BlueprintType)
namespace EMeshComponentUpdateFlag {
    enum Type {
        AlwaysTickPoseAndRefreshBones,
        AlwaysTickPose,
        OnlyTickPoseWhenRendered,
    };
}

