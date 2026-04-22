#include "MeshProxySettings.h"

FMeshProxySettings::FMeshProxySettings() {
    this->ScreenSize = 0;
    this->TextureWidth = 0;
    this->TextureHeight = 0;
    this->bExportNormalMap = false;
    this->bExportMetallicMap = false;
    this->bExportRoughnessMap = false;
    this->bExportSpecularMap = false;
    this->bCalculateCorrectLODModel = false;
    this->MergeDistance = 0.00f;
    this->HardAngleThreshold = 0.00f;
    this->LightMapResolution = 0;
    this->bRecalculateNormals = false;
    this->bBakeVertexData = false;
    this->bUseLandscapeCulling = false;
    this->LandscapeCullingPrecision = ELandscapeCullingPrecision::High;
}

