#pragma once
#include "CoreMinimal.h"
#include "ExposedValueHandler.h"
#include "AnimNode_Base.generated.h"

USTRUCT(BlueprintType)
struct ENGINE_API FAnimNode_Base {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FExposedValueHandler EvaluateGraphExposedInputs;
    
    FAnimNode_Base();
};

