#pragma once
#include "CoreMinimal.h"
#include "NiagaraTypeDefinition.generated.h"

class UStruct;

USTRUCT(BlueprintType)
struct NIAGARA_API FNiagaraTypeDefinition {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    UStruct* Struct;
    
    FNiagaraTypeDefinition();
};

