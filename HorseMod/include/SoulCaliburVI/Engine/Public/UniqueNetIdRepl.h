#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=UniqueNetIdWrapper -FallbackName=UniqueNetIdWrapper
#include "UniqueNetIdRepl.generated.h"

USTRUCT(BlueprintType)
struct FUniqueNetIdRepl : public FUniqueNetIdWrapper {
    GENERATED_BODY()
public:
    ENGINE_API FUniqueNetIdRepl();
};

