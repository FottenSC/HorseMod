#pragma once
#include "CoreMinimal.h"
#include "NiagaraEffectRendererProperties.h"
#include "NiagaraMeshRendererProperties.generated.h"

class UStaticMesh;

UCLASS(Blueprintable, EditInlineNew)
class UNiagaraMeshRendererProperties : public UNiagaraEffectRendererProperties {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    UStaticMesh* ParticleMesh;
    
    UNiagaraMeshRendererProperties();

};

