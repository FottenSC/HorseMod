#pragma once
#include "CoreMinimal.h"
#include "NiagaraEmitterBurst.generated.h"

USTRUCT(BlueprintType)
struct FNiagaraEmitterBurst {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float Time;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float TimeRange;
    
    UPROPERTY(EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 SpawnMinimum;
    
    UPROPERTY(EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 SpawnMaximum;
    
    NIAGARA_API FNiagaraEmitterBurst();
};

