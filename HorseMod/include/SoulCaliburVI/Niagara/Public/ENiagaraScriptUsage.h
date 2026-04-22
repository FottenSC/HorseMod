#pragma once
#include "CoreMinimal.h"
#include "ENiagaraScriptUsage.generated.h"

UENUM(BlueprintType)
enum class ENiagaraScriptUsage : uint8 {
    Function,
    Module,
    SpawnScript,
    SpawnScriptInterpolated,
    UpdateScript,
    EffectScript,
};

