#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=AnimInstanceProxy -FallbackName=AnimInstanceProxy
#include "LuxCharaAnimInstanceProxy.generated.h"

USTRUCT(BlueprintType)
struct LUXORGAME_API FLuxCharaAnimInstanceProxy : public FAnimInstanceProxy {
    GENERATED_BODY()
public:
    FLuxCharaAnimInstanceProxy();
};

