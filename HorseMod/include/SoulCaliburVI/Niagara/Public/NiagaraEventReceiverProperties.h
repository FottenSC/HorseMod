#pragma once
#include "CoreMinimal.h"
#include "NiagaraEventReceiverProperties.generated.h"

class UNiagaraEventReceiverEmitterAction;

USTRUCT(BlueprintType)
struct FNiagaraEventReceiverProperties {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FName Name;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FName SourceEventGenerator;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FName SourceEmitter;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    TArray<UNiagaraEventReceiverEmitterAction*> EmitterActions;
    
    NIAGARA_API FNiagaraEventReceiverProperties();
};

