#include "SkeletalMesh.h"

USkeletalMesh::USkeletalMesh() {
    this->Skeleton = NULL;
    this->SkelMirrorAxis = EAxis::X;
    this->SkelMirrorFlipAxis = EAxis::Z;
    this->bUseFullPrecisionUVs = false;
    this->bHasBeenSimplified = false;
    this->bHasVertexColors = false;
    this->bEnablePerPolyCollision = false;
    this->BodySetup = NULL;
    this->PhysicsAsset = NULL;
    this->ShadowPhysicsAsset = NULL;
    this->PostProcessAnimBlueprint = NULL;
}

int32 USkeletalMesh::NumSockets() const {
    return 0;
}

bool USkeletalMesh::IsSectionUsingCloth(int32 InSectionIndex, bool bCheckCorrespondingSections) const {
    return false;
}

USkeletalMeshSocket* USkeletalMesh::GetSocketByIndex(int32 index) const {
    return NULL;
}

UNodeMappingContainer* USkeletalMesh::GetNodeMappingContainer(UBlueprint* SourceAsset) const {
    return NULL;
}

FBoxSphereBounds USkeletalMesh::GetImportedBounds() {
    return FBoxSphereBounds{};
}

FBoxSphereBounds USkeletalMesh::GetBounds() {
    return FBoxSphereBounds{};
}

USkeletalMeshSocket* USkeletalMesh::FindSocketAndIndex(FName InSocketName, int32& OutIndex) const {
    return NULL;
}

USkeletalMeshSocket* USkeletalMesh::FindSocket(FName InSocketName) const {
    return NULL;
}


