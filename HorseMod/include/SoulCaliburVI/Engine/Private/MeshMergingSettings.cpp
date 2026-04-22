#include "MeshMergingSettings.h"

FMeshMergingSettings::FMeshMergingSettings() {
    this->bGenerateLightMapUV = false;
    this->TargetLightMapResolution = 0;
    this->bImportVertexColors = false;
    this->bPivotPointAtZero = false;
    this->bMergePhysicsData = false;
    this->bMergeMaterials = false;
    this->bBakeVertexDataToMesh = false;
    this->bUseVertexDataForBakingMaterial = false;
    this->bUseTextureBinning = false;
    this->bCalculateCorrectLODModel = false;
    this->LODSelectionType = EMeshLODSelectionType::AllLODs;
    this->ExportSpecificLOD = 0;
    this->SpecificLOD = 0;
    this->bUseLandscapeCulling = false;
    this->bExportNormalMap = false;
    this->bExportMetallicMap = false;
    this->bExportRoughnessMap = false;
    this->bExportSpecularMap = false;
    this->MergedMaterialAtlasResolution = 0;
}

