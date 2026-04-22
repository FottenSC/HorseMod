#include "StaticMesh.h"

UStaticMesh::UStaticMesh() {
    this->MinLOD = 0;
    this->LightmapUVDensity = 0.00f;
    this->LightMapResolution = 4;
    this->LightMapCoordinateIndex = 0;
    this->DistanceFieldSelfShadowBias = 0.00f;
    this->bGenerateMeshDistanceField = false;
    this->BodySetup = NULL;
    this->LODForCollision = 0;
    this->bStripComplexCollisionForConsole = false;
    this->bHasNavigationData = true;
    this->bSupportUniformlyDistributedSampling = false;
    this->LpvBiasMultiplier = 1.00f;
    this->bAllowCPUAccess = false;
    this->ElementToIgnoreForTexFactor = -1;
    this->NavCollision = NULL;
}

int32 UStaticMesh::GetNumSections(int32 InLOD) const {
    return 0;
}

int32 UStaticMesh::GetNumLODs() const {
    return 0;
}

int32 UStaticMesh::GetMaterialIndex(FName MaterialSlotName) const {
    return 0;
}

UMaterialInterface* UStaticMesh::GetMaterial(int32 MaterialIndex) const {
    return NULL;
}

FBoxSphereBounds UStaticMesh::GetBounds() const {
    return FBoxSphereBounds{};
}

FBox UStaticMesh::GetBoundingBox() const {
    return FBox{};
}


