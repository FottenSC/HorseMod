#pragma once
#include "CoreMinimal.h"
#include "TickFunction.h"
#include "StartAsyncSimulationFunction.generated.h"

USTRUCT(BlueprintType)
struct FStartAsyncSimulationFunction : public FTickFunction {
    GENERATED_BODY()
public:
    ENGINE_API FStartAsyncSimulationFunction();
};

template<>
struct TStructOpsTypeTraits<FStartAsyncSimulationFunction> : public TStructOpsTypeTraitsBase2<FStartAsyncSimulationFunction>
{
    enum
    {
        WithCopy = false
    };
};

