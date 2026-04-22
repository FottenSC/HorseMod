#pragma once
#include "CoreMinimal.h"
#include "TickFunction.h"
#include "PrimitiveComponentPostPhysicsTickFunction.generated.h"

USTRUCT(BlueprintType)
struct FPrimitiveComponentPostPhysicsTickFunction : public FTickFunction {
    GENERATED_BODY()
public:
    ENGINE_API FPrimitiveComponentPostPhysicsTickFunction();
};

template<>
struct TStructOpsTypeTraits<FPrimitiveComponentPostPhysicsTickFunction> : public TStructOpsTypeTraitsBase2<FPrimitiveComponentPostPhysicsTickFunction>
{
    enum
    {
        WithCopy = false
    };
};

