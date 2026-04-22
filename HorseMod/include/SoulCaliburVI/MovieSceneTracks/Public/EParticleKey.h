#pragma once
#include "CoreMinimal.h"
#include "EParticleKey.generated.h"

UENUM(BlueprintType)
namespace EParticleKey {
    enum Type {
        Activate,
        Deactivate,
        Trigger,
    };
}

