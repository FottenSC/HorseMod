#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=ParticleRandomSeedInfo -FallbackName=ParticleRandomSeedInfo
#include "LuxParticleModuleLocationPrimitiveSphere.h"
#include "LuxParticleModuleLocationPrimitiveSphere_Seeded.generated.h"

UCLASS(Blueprintable, EditInlineNew)
class ULuxParticleModuleLocationPrimitiveSphere_Seeded : public ULuxParticleModuleLocationPrimitiveSphere {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FParticleRandomSeedInfo RandomSeedInfo;
    
    ULuxParticleModuleLocationPrimitiveSphere_Seeded();

};

