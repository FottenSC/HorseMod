#pragma once
#include "CoreMinimal.h"
#include "ParticleModuleKillBase.h"
#include "RawDistributionVector.h"
#include "ParticleModuleKillBox.generated.h"

UCLASS(Blueprintable, EditInlineNew)
class UParticleModuleKillBox : public UParticleModuleKillBase {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FRawDistributionVector LowerLeftCorner;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FRawDistributionVector UpperRightCorner;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bAbsolute: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bKillInside: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bAxisAlignedAndFixedSize: 1;
    
    UParticleModuleKillBox();

};

