#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=ParticleModuleLocationPrimitiveCylinder -FallbackName=ParticleModuleLocationPrimitiveCylinder
#include "LuxParticleModuleLocationPrimitiveCylinder.generated.h"

UCLASS(Blueprintable, EditInlineNew)
class ULuxParticleModuleLocationPrimitiveCylinder : public UParticleModuleLocationPrimitiveCylinder {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float ChainRate;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bChainRandom;
    
    ULuxParticleModuleLocationPrimitiveCylinder();

};

