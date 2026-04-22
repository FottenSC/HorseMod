#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=ParticleRandomSeedInfo -FallbackName=ParticleRandomSeedInfo
#include "LuxParticleModuleRotationRate.h"
#include "LuxParticleModuleRotationRate_Seeded.generated.h"

UCLASS(Blueprintable, EditInlineNew)
class ULuxParticleModuleRotationRate_Seeded : public ULuxParticleModuleRotationRate {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FParticleRandomSeedInfo RandomSeedInfo;
    
    ULuxParticleModuleRotationRate_Seeded();

};

