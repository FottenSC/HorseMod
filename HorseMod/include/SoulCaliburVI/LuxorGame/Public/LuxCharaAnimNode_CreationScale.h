#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=AnimNode_Base -FallbackName=AnimNode_Base
#include "LuxCharaAnimNode_CreationScale.generated.h"

USTRUCT(BlueprintType)
struct LUXORGAME_API FLuxCharaAnimNode_CreationScale : public FAnimNode_Base {
    GENERATED_BODY()
public:
    FLuxCharaAnimNode_CreationScale();
};

