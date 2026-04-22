#pragma once
#include "CoreMinimal.h"
#include "NiagaraDataSetProperties.h"
#include "NiagaraEventGeneratorProperties.generated.h"

USTRUCT(BlueprintType)
struct FNiagaraEventGeneratorProperties {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    int32 MaxEventsPerFrame;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FNiagaraDataSetProperties SetProps;
    
    NIAGARA_API FNiagaraEventGeneratorProperties();
};

