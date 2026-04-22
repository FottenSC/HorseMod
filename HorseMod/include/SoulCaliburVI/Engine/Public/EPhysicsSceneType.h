#pragma once
#include "CoreMinimal.h"
#include "EPhysicsSceneType.generated.h"

UENUM(BlueprintType)
enum EPhysicsSceneType {
    PST_Sync,
    PST_Cloth,
    PST_Async,
};

