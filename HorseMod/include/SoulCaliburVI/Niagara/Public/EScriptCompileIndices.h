#pragma once
#include "CoreMinimal.h"
#include "EScriptCompileIndices.generated.h"

UENUM(BlueprintType)
enum class EScriptCompileIndices : uint8 {
    SpawnScript,
    UpdateScript,
    EventScript,
};

