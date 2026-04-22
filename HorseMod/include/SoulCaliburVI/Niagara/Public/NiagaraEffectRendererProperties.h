#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
#include "NiagaraEffectRendererProperties.generated.h"

UCLASS(Abstract, Blueprintable)
class NIAGARA_API UNiagaraEffectRendererProperties : public UObject {
    GENERATED_BODY()
public:
    UNiagaraEffectRendererProperties();

};

