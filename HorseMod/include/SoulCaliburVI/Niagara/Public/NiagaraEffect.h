#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
#include "NiagaraEmitterHandle.h"
#include "NiagaraEmitterInternalVariableBinding.h"
#include "NiagaraParameterBinding.h"
#include "NiagaraEffect.generated.h"

class UNiagaraScript;

UCLASS(Blueprintable)
class NIAGARA_API UNiagaraEffect : public UObject {
    GENERATED_BODY()
public:
protected:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FNiagaraEmitterHandle> EmitterHandles;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    UNiagaraScript* EffectScript;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FNiagaraParameterBinding> ParameterBindings;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FNiagaraParameterBinding> DataInterfaceBindings;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FNiagaraEmitterInternalVariableBinding> InternalEmitterVariableBindings;
    
public:
    UNiagaraEffect();

};

