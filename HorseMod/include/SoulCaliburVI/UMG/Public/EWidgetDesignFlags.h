#pragma once
#include "CoreMinimal.h"
#include "EWidgetDesignFlags.generated.h"

UENUM(BlueprintType)
namespace EWidgetDesignFlags {
    enum Type {
        None,
        Designing,
        ShowOutline,
        ExecutePreConstruct = 4,
    };
}

