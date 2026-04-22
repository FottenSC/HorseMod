#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=BoxSphereBounds -FallbackName=BoxSphereBounds
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Color -FallbackName=Color
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=LinearColor -FallbackName=LinearColor
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Rotator -FallbackName=Rotator
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Vector -FallbackName=Vector
#include "EMeshComponentUpdateFlag.h"
#include "EPhysBodyOp.h"
#include "MeshComponent.h"
#include "SkelMeshComponentLODInfo.h"
#include "SkelMeshSkinWeightInfo.h"
#include "SkinnedMeshComponent.generated.h"

class UPhysicsAsset;
class USkeletalMesh;
class USkinnedMeshComponent;

UCLASS(Abstract, Blueprintable, EditInlineNew, ClassGroup=Custom, meta=(BlueprintSpawnableComponent))
class ENGINE_API USkinnedMeshComponent : public UMeshComponent {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    USkeletalMesh* SkeletalMesh;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Export, meta=(AllowPrivateAccess=true))
    TWeakObjectPtr<USkinnedMeshComponent> MasterPoseComponent;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bUseBoundsFromMasterPoseComponent: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    UPhysicsAsset* PhysicsAssetOverride;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    int32 ForcedLodModel;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    int32 MinLodModel;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    TArray<FSkelMeshComponentLODInfo> LODInfo;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float StreamingDistanceMultiplier;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FColor WireframeColor;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bForceWireframe: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bDisplayBones: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bDisableMorphTarget: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bHideSkin: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bPerBoneMotionBlur: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bComponentUseFixedSkelBounds: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bConsiderAllBodiesForBounds: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TEnumAsByte<EMeshComponentUpdateFlag::Type> MeshComponentUpdateFlag;
    
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    uint32 bForceMeshObjectUpdate: 1;
    
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    uint32 bCanHighlightSelectedSections: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    uint32 bRecentlyRendered: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    uint8 CustomSortAlternateIndexMode;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bCastCapsuleDirectShadow: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bCastCapsuleIndirectShadow: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float CapsuleIndirectShadowMinVisibility;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    uint32 bCPUSkinning: 1;
    
protected:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    FBoxSphereBounds CachedLocalBounds;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    bool bCachedLocalBoundsUpToDate;
    
public:
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bEnableUpdateRateOptimizations;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bDisplayDebugUpdateRateOptimizations;
    
    USkinnedMeshComponent(const FObjectInitializer& ObjectInitializer);

    UFUNCTION(BlueprintCallable)
    void UnHideBoneByName(FName BoneName);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    void TransformToBoneSpace(FName BoneName, FVector InPosition, FRotator inRotation, FVector& OutPosition, FRotator& OutRotation) const;
    
    UFUNCTION(BlueprintCallable)
    void TransformFromBoneSpace(FName BoneName, FVector InPosition, FRotator inRotation, FVector& OutPosition, FRotator& OutRotation);
    
    UFUNCTION(BlueprintCallable)
    void ShowMaterialSection(int32 MaterialID, bool bShow, int32 LODIndex);
    
    UFUNCTION(BlueprintCallable)
    void SetVertexColorOverride_LinearColor(int32 LODIndex, const TArray<FLinearColor>& VertexColors);
    
    UFUNCTION(BlueprintCallable)
    void SetSkinWeightOverride(int32 LODIndex, const TArray<FSkelMeshSkinWeightInfo>& SkinWeights);
    
    UFUNCTION(BlueprintCallable)
    void SetSkeletalMesh(USkeletalMesh* NewMesh, bool bReinitPose);
    
    UFUNCTION(BlueprintCallable)
    void SetPhysicsAsset(UPhysicsAsset* NewPhysicsAsset, bool bForceReInit);
    
    UFUNCTION(BlueprintCallable)
    void SetMinLOD(int32 InNewMinLOD);
    
    UFUNCTION(BlueprintCallable)
    void SetMasterPoseComponent(USkinnedMeshComponent* NewMasterBoneComponent);
    
    UFUNCTION(BlueprintCallable)
    void SetForcedLOD(int32 InNewForcedLOD);
    
    UFUNCTION(BlueprintCallable)
    void SetCastCapsuleIndirectShadow(bool bNewValue);
    
    UFUNCTION(BlueprintCallable)
    void SetCastCapsuleDirectShadow(bool bNewValue);
    
    UFUNCTION(BlueprintCallable)
    void SetCapsuleIndirectShadowMinVisibility(float NewValue);
    
    UFUNCTION(BlueprintCallable)
    bool IsBoneHiddenByName(FName BoneName);
    
    UFUNCTION(BlueprintCallable)
    void HideBoneByName(FName BoneName, TEnumAsByte<EPhysBodyOp> PhysBodyOption);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FName GetSocketBoneName(FName InSocketName) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FName GetParentBone(FName BoneName) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    int32 GetNumBones() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FName GetBoneName(int32 BoneIndex) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    int32 GetBoneIndex(FName BoneName) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FName FindClosestBone_K2(FVector TestLocation, FVector& BoneLocation, float IgnoreScale, bool bRequirePhysicsAsset) const;
    
    UFUNCTION(BlueprintCallable)
    void ClearVertexColorOverride(int32 LODIndex);
    
    UFUNCTION(BlueprintCallable)
    void ClearSkinWeightOverride(int32 LODIndex);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool BoneIsChildOf(FName BoneName, FName ParentBoneName) const;
    
};

