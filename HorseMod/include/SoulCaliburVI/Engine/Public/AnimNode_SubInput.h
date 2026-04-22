#pragma once
#include "CoreMinimal.h"
#include "AnimNode_Base.h"
#include "AnimNode_SubInput.generated.h"

USTRUCT(BlueprintType)
struct ENGINE_API FAnimNode_SubInput : public FAnimNode_Base {
    GENERATED_BODY()
public:
    FAnimNode_SubInput();
};

