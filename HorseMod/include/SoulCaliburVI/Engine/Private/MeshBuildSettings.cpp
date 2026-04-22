#include "MeshBuildSettings.h"

FMeshBuildSettings::FMeshBuildSettings() {
    this->bUseMikkTSpace = false;
    this->bRecomputeNormals = false;
    this->bRecomputeTangents = false;
    this->bRemoveDegenerates = false;
    this->bBuildAdjacencyBuffer = false;
    this->bBuildReversedIndexBuffer = false;
    this->bUseHighPrecisionTangentBasis = false;
    this->bUseFullPrecisionUVs = false;
    this->bGenerateLightmapUVs = false;
    this->MinLightmapResolution = 0;
    this->SrcLightmapIndex = 0;
    this->DstLightmapIndex = 0;
    this->BuildScale = 0.00f;
    this->DistanceFieldResolutionScale = 0.00f;
    this->bGenerateDistanceFieldAsIfTwoSided = false;
    this->DistanceFieldBias = 0.00f;
    this->DistanceFieldReplacementMesh = NULL;
}

