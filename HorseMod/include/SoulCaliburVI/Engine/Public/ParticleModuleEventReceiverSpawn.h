#pragma once
#include "CoreMinimal.h"
#include "ParticleModuleEventReceiverBase.h"
#include "RawDistributionFloat.h"
#include "RawDistributionVector.h"
#include "ParticleModuleEventReceiverSpawn.generated.h"

class UPhysicalMaterial;

UCLASS(Blueprintable, EditInlineNew)
class UParticleModuleEventReceiverSpawn : public UParticleModuleEventReceiverBase {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FRawDistributionFloat SpawnCount;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bUseParticleTime: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bUsePSysLocation: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bInheritVelocity: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FRawDistributionVector InheritVelocityScale;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<UPhysicalMaterial*> PhysicalMaterials;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bBanPhysicalMaterials: 1;
    
    UParticleModuleEventReceiverSpawn();

};

