#include "SkinnedMeshComponent.h"

USkinnedMeshComponent::USkinnedMeshComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->bAutoActivate = true;
    this->bCanEverAffectNavigation = false;
    this->CanCharacterStepUpOn = ECB_Owner;
    this->SkeletalMesh = NULL;
    this->bUseBoundsFromMasterPoseComponent = false;
    this->PhysicsAssetOverride = NULL;
    this->ForcedLodModel = 0;
    this->MinLodModel = 0;
    this->StreamingDistanceMultiplier = 1.00f;
    this->bForceWireframe = false;
    this->bDisplayBones = false;
    this->bDisableMorphTarget = false;
    this->bHideSkin = false;
    this->bPerBoneMotionBlur = true;
    this->bComponentUseFixedSkelBounds = false;
    this->bConsiderAllBodiesForBounds = false;
    this->MeshComponentUpdateFlag = EMeshComponentUpdateFlag::AlwaysTickPoseAndRefreshBones;
    this->bForceMeshObjectUpdate = false;
    this->bCanHighlightSelectedSections = false;
    this->bRecentlyRendered = false;
    this->CustomSortAlternateIndexMode = 0;
    this->bCastCapsuleDirectShadow = false;
    this->bCastCapsuleIndirectShadow = false;
    this->CapsuleIndirectShadowMinVisibility = 0.10f;
    this->bCPUSkinning = false;
    this->bCachedLocalBoundsUpToDate = false;
    this->bEnableUpdateRateOptimizations = false;
    this->bDisplayDebugUpdateRateOptimizations = false;
}

void USkinnedMeshComponent::UnHideBoneByName(FName BoneName) {
}

void USkinnedMeshComponent::TransformToBoneSpace(FName BoneName, FVector InPosition, FRotator inRotation, FVector& OutPosition, FRotator& OutRotation) const {
}

void USkinnedMeshComponent::TransformFromBoneSpace(FName BoneName, FVector InPosition, FRotator inRotation, FVector& OutPosition, FRotator& OutRotation) {
}

void USkinnedMeshComponent::ShowMaterialSection(int32 MaterialID, bool bShow, int32 LODIndex) {
}

void USkinnedMeshComponent::SetVertexColorOverride_LinearColor(int32 LODIndex, const TArray<FLinearColor>& VertexColors) {
}

void USkinnedMeshComponent::SetSkinWeightOverride(int32 LODIndex, const TArray<FSkelMeshSkinWeightInfo>& SkinWeights) {
}

void USkinnedMeshComponent::SetSkeletalMesh(USkeletalMesh* NewMesh, bool bReinitPose) {
}

void USkinnedMeshComponent::SetPhysicsAsset(UPhysicsAsset* NewPhysicsAsset, bool bForceReInit) {
}

void USkinnedMeshComponent::SetMinLOD(int32 InNewMinLOD) {
}

void USkinnedMeshComponent::SetMasterPoseComponent(USkinnedMeshComponent* NewMasterBoneComponent) {
}

void USkinnedMeshComponent::SetForcedLOD(int32 InNewForcedLOD) {
}

void USkinnedMeshComponent::SetCastCapsuleIndirectShadow(bool bNewValue) {
}

void USkinnedMeshComponent::SetCastCapsuleDirectShadow(bool bNewValue) {
}

void USkinnedMeshComponent::SetCapsuleIndirectShadowMinVisibility(float NewValue) {
}

bool USkinnedMeshComponent::IsBoneHiddenByName(FName BoneName) {
    return false;
}

void USkinnedMeshComponent::HideBoneByName(FName BoneName, TEnumAsByte<EPhysBodyOp> PhysBodyOption) {
}

FName USkinnedMeshComponent::GetSocketBoneName(FName InSocketName) const {
    return NAME_None;
}

FName USkinnedMeshComponent::GetParentBone(FName BoneName) const {
    return NAME_None;
}

int32 USkinnedMeshComponent::GetNumBones() const {
    return 0;
}

FName USkinnedMeshComponent::GetBoneName(int32 BoneIndex) const {
    return NAME_None;
}

int32 USkinnedMeshComponent::GetBoneIndex(FName BoneName) const {
    return 0;
}

FName USkinnedMeshComponent::FindClosestBone_K2(FVector TestLocation, FVector& BoneLocation, float IgnoreScale, bool bRequirePhysicsAsset) const {
    return NAME_None;
}

void USkinnedMeshComponent::ClearVertexColorOverride(int32 LODIndex) {
}

void USkinnedMeshComponent::ClearSkinWeightOverride(int32 LODIndex) {
}

bool USkinnedMeshComponent::BoneIsChildOf(FName BoneName, FName ParentBoneName) const {
    return false;
}


