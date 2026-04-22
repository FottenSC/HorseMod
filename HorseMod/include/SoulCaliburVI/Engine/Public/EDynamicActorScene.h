#pragma once
#include "CoreMinimal.h"
#include "EDynamicActorScene.generated.h"

UENUM(BlueprintType)
enum class EDynamicActorScene : uint8 {
    Default,
    UseSyncScene,
    UseAsyncScene,
};

