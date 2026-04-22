#pragma once
#include "CoreMinimal.h"
#include "ParticleModuleKillBase.h"
#include "RawDistributionFloat.h"
#include "ParticleModuleKillHeight.generated.h"

UCLASS(Blueprintable, EditInlineNew)
class UParticleModuleKillHeight : public UParticleModuleKillBase {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FRawDistributionFloat Height;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bAbsolute: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bFloor: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bApplyPSysScale: 1;
    
    UParticleModuleKillHeight();

};

