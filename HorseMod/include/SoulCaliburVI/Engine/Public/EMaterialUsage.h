#pragma once
#include "CoreMinimal.h"
#include "EMaterialUsage.generated.h"

UENUM(BlueprintType)
enum EMaterialUsage {
    MATUSAGE_SkeletalMesh,
    MATUSAGE_ParticleSprites,
    MATUSAGE_BeamTrails,
    MATUSAGE_MeshParticles,
    MATUSAGE_StaticLighting,
    MATUSAGE_MorphTargets,
    MATUSAGE_SplineMesh,
    MATUSAGE_InstancedStaticMeshes,
    MATUSAGE_Clothing,
    MATUSAGE_NiagaraSprites,
    MATUSAGE_NiagaraRibbons,
    MATUSAGE_NiagaraMeshParticles,
};

