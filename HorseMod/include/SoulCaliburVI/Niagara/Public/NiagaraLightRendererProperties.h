#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Vector -FallbackName=Vector
#include "NiagaraEffectRendererProperties.h"
#include "NiagaraLightRendererProperties.generated.h"

UCLASS(Blueprintable, EditInlineNew)
class UNiagaraLightRendererProperties : public UNiagaraEffectRendererProperties {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float RadiusScale;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FVector ColorAdd;
    
    UNiagaraLightRendererProperties();

};

