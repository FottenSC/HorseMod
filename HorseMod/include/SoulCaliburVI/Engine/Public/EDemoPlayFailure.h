#pragma once
#include "CoreMinimal.h"
#include "EDemoPlayFailure.generated.h"

UENUM(BlueprintType)
namespace EDemoPlayFailure {
    enum Type {
        Generic,
        DemoNotFound,
        Corrupt,
        InvalidVersion,
    };
}

