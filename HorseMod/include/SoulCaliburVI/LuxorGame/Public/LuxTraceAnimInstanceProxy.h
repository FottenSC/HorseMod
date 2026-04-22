#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=AnimInstanceProxy -FallbackName=AnimInstanceProxy
#include "LuxTraceAnimInstanceProxy.generated.h"

USTRUCT(BlueprintType)
struct LUXORGAME_API FLuxTraceAnimInstanceProxy : public FAnimInstanceProxy {
    GENERATED_BODY()
public:
    FLuxTraceAnimInstanceProxy();
};

