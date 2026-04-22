#pragma once
#include "CoreMinimal.h"
#include "ParticleModuleSizeBase.h"
#include "RawDistributionVector.h"
#include "ParticleModuleSizeMultiplyLife.generated.h"

UCLASS(Blueprintable, EditInlineNew, MinimalAPI)
class UParticleModuleSizeMultiplyLife : public UParticleModuleSizeBase {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FRawDistributionVector LifeMultiplier;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 MultiplyX: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 MultiplyY: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 MultiplyZ: 1;
    
    UParticleModuleSizeMultiplyLife();

};

