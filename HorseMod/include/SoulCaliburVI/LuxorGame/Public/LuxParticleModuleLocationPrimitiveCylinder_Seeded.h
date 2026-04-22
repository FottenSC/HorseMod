#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=ParticleRandomSeedInfo -FallbackName=ParticleRandomSeedInfo
#include "LuxParticleModuleLocationPrimitiveCylinder.h"
#include "LuxParticleModuleLocationPrimitiveCylinder_Seeded.generated.h"

UCLASS(Blueprintable, EditInlineNew)
class ULuxParticleModuleLocationPrimitiveCylinder_Seeded : public ULuxParticleModuleLocationPrimitiveCylinder {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FParticleRandomSeedInfo RandomSeedInfo;
    
    ULuxParticleModuleLocationPrimitiveCylinder_Seeded();

};

