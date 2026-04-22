#pragma once
#include "CoreMinimal.h"
#include "NiagaraBool.generated.h"

USTRUCT(BlueprintType)
struct FNiagaraBool {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    int32 Value;
    
    NIAGARA_API FNiagaraBool();
};

