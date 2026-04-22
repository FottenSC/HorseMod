#include "SkeletalMeshOptimizationSettings.h"

FSkeletalMeshOptimizationSettings::FSkeletalMeshOptimizationSettings() {
    this->ReductionMethod = SMOT_NumOfTriangles;
    this->NumOfTrianglesPercentage = 0.00f;
    this->MaxDeviationPercentage = 0.00f;
    this->WeldingThreshold = 0.00f;
    this->bRecalcNormals = false;
    this->NormalsThreshold = 0.00f;
    this->SilhouetteImportance = SMOI_Off;
    this->TextureImportance = SMOI_Off;
    this->ShadingImportance = SMOI_Off;
    this->SkinningImportance = SMOI_Off;
    this->BoneReductionRatio = 0.00f;
    this->MaxBonesPerVertex = 0;
    this->BaseLOD = 0;
    this->BakePose = NULL;
}

