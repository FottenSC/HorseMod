#pragma once
#include "CoreMinimal.h"
#include "ParticleModuleRotationRateBase.h"
#include "RawDistributionFloat.h"
#include "ParticleModuleRotationRate.generated.h"

UCLASS(Blueprintable, EditInlineNew)
class ENGINE_API UParticleModuleRotationRate : public UParticleModuleRotationRateBase {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FRawDistributionFloat StartRotationRate;
    
    UParticleModuleRotationRate();

};

