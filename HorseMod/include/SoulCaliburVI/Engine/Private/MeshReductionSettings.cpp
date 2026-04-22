#include "MeshReductionSettings.h"

FMeshReductionSettings::FMeshReductionSettings() {
    this->PercentTriangles = 0.00f;
    this->MaxDeviation = 0.00f;
    this->PixelError = 0.00f;
    this->WeldingThreshold = 0.00f;
    this->HardAngleThreshold = 0.00f;
    this->SilhouetteImportance = EMeshFeatureImportance::Off;
    this->TextureImportance = EMeshFeatureImportance::Off;
    this->ShadingImportance = EMeshFeatureImportance::Off;
    this->bRecalculateNormals = false;
    this->BaseLODModel = 0;
    this->bGenerateUniqueLightmapUVs = false;
    this->bKeepSymmetry = false;
    this->bVisibilityAided = false;
    this->bCullOccluded = false;
    this->VisibilityAggressiveness = EMeshFeatureImportance::Off;
    this->VertexColorImportance = EMeshFeatureImportance::Off;
}

