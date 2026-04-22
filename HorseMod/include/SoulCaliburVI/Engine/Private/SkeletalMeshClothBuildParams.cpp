#include "SkeletalMeshClothBuildParams.h"

FSkeletalMeshClothBuildParams::FSkeletalMeshClothBuildParams() {
    this->LODIndex = 0;
    this->SourceSection = 0;
    this->bRemoveFromMesh = false;
    this->bTryAutoFix = false;
    this->AutoFixThreshold = 0.00f;
    this->SimulatedParticleMaxDistance = 0.00f;
}

