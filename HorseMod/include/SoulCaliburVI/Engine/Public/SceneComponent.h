#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Quat -FallbackName=Quat
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Rotator -FallbackName=Rotator
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Transform -FallbackName=Transform
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Vector -FallbackName=Vector
#include "ActorComponent.h"
#include "EAttachLocation.h"
#include "EAttachmentRule.h"
#include "EComponentMobility.h"
#include "EDetachmentRule.h"
#include "EDetailMode.h"
#include "ERelativeTransformSpace.h"
#include "HitResult.h"
#include "PhysicsVolumeChangedDelegate.h"
#include "SceneComponent.generated.h"

class APhysicsVolume;
class USceneComponent;

UCLASS(Blueprintable, ClassGroup=Custom, meta=(BlueprintSpawnableComponent))
class ENGINE_API USceneComponent : public UActorComponent {
    GENERATED_BODY()
public:
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, ReplicatedUsing=OnRep_AttachParent, meta=(AllowPrivateAccess=true))
    USceneComponent* AttachParent;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, Transient, ReplicatedUsing=OnRep_AttachChildren, meta=(AllowPrivateAccess=true))
    TArray<USceneComponent*> AttachChildren;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Export, Transient, meta=(AllowPrivateAccess=true))
    TSet<USceneComponent*> ClientAttachedChildren;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, ReplicatedUsing=OnRep_AttachSocketName, meta=(AllowPrivateAccess=true))
    FName AttachSocketName;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    uint32 bWorldToComponentUpdated: 1;
    
public:
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, ReplicatedUsing=OnRep_Transform, meta=(AllowPrivateAccess=true))
    uint32 bAbsoluteLocation: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, ReplicatedUsing=OnRep_Transform, meta=(AllowPrivateAccess=true))
    uint32 bAbsoluteRotation: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, ReplicatedUsing=OnRep_Transform, meta=(AllowPrivateAccess=true))
    uint32 bAbsoluteScale: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, ReplicatedUsing=OnRep_Visibility, meta=(AllowPrivateAccess=true))
    uint32 bVisible: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bHiddenInGame: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bShouldUpdatePhysicsVolume: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bBoundsChangeTriggersStreamingDataRebuild: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bUseAttachParentBound: 1;
    
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bAbsoluteTranslation: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    TWeakObjectPtr<APhysicsVolume> PhysicsVolume;
    
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, ReplicatedUsing=OnRep_Transform, meta=(AllowPrivateAccess=true))
    FVector RelativeLocation;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, ReplicatedUsing=OnRep_Transform, meta=(AllowPrivateAccess=true))
    FRotator RelativeRotation;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Interp, ReplicatedUsing=OnRep_Transform, meta=(AllowPrivateAccess=true))
    FVector RelativeScale3D;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FVector RelativeTranslation;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TEnumAsByte<EComponentMobility::Type> Mobility;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TEnumAsByte<EDetailMode> DetailMode;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FVector ComponentVelocity;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FPhysicsVolumeChanged PhysicsVolumeChangedDelegate;
    
    USceneComponent(const FObjectInitializer& ObjectInitializer);

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UFUNCTION(BlueprintCallable)
    void ToggleVisibility(bool bPropagateToChildren);
    
    UFUNCTION(BlueprintCallable)
    bool SnapTo(USceneComponent* InParent, FName InSocketName);
    
    UFUNCTION(BlueprintCallable)
    void SetWorldScale3D(FVector NewScale);
    
    UFUNCTION(BlueprintCallable)
    void SetVisibility(bool bNewVisibility, bool bPropagateToChildren);
    
    UFUNCTION(BlueprintCallable)
    void SetRelativeScale3D(FVector NewScale3D);
    
    UFUNCTION(BlueprintCallable)
    void SetHiddenInGame(bool NewHidden, bool bPropagateToChildren);
    
    UFUNCTION(BlueprintCallable)
    void SetAbsolute(bool bNewAbsoluteLocation, bool bNewAbsoluteRotation, bool bNewAbsoluteScale);
    
    UFUNCTION(BlueprintCallable)
    void ResetRelativeTransform();
    
private:
    UFUNCTION(BlueprintCallable)
    void OnRep_Visibility(bool OldValue);
    
    UFUNCTION(BlueprintCallable)
    void OnRep_Transform();
    
    UFUNCTION(BlueprintCallable)
    void OnRep_AttachSocketName();
    
    UFUNCTION(BlueprintCallable)
    void OnRep_AttachParent();
    
    UFUNCTION(BlueprintCallable)
    void OnRep_AttachChildren();
    
public:
    UFUNCTION(BlueprintCallable)
    void K2_SetWorldTransform(const FTransform& NewTransform, bool bSweep, FHitResult& SweepHitResult, bool bTeleport);
    
    UFUNCTION(BlueprintCallable)
    void K2_SetWorldRotation(FRotator NewRotation, bool bSweep, FHitResult& SweepHitResult, bool bTeleport);
    
    UFUNCTION(BlueprintCallable)
    void K2_SetWorldLocationAndRotation(FVector NewLocation, FRotator NewRotation, bool bSweep, FHitResult& SweepHitResult, bool bTeleport);
    
    UFUNCTION(BlueprintCallable)
    void K2_SetWorldLocation(FVector NewLocation, bool bSweep, FHitResult& SweepHitResult, bool bTeleport);
    
    UFUNCTION(BlueprintCallable)
    void K2_SetRelativeTransform(const FTransform& NewTransform, bool bSweep, FHitResult& SweepHitResult, bool bTeleport);
    
    UFUNCTION(BlueprintCallable)
    void K2_SetRelativeRotation(FRotator NewRotation, bool bSweep, FHitResult& SweepHitResult, bool bTeleport);
    
    UFUNCTION(BlueprintCallable)
    void K2_SetRelativeLocationAndRotation(FVector NewLocation, FRotator NewRotation, bool bSweep, FHitResult& SweepHitResult, bool bTeleport);
    
    UFUNCTION(BlueprintCallable)
    void K2_SetRelativeLocation(FVector NewLocation, bool bSweep, FHitResult& SweepHitResult, bool bTeleport);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FTransform K2_GetComponentToWorld() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FVector K2_GetComponentScale() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FRotator K2_GetComponentRotation() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FVector K2_GetComponentLocation() const;
    
    UFUNCTION(BlueprintCallable)
    void K2_DetachFromComponent(EDetachmentRule LocationRule, EDetachmentRule RotationRule, EDetachmentRule ScaleRule, bool bCallModify);
    
    UFUNCTION(BlueprintCallable)
    bool K2_AttachToComponent(USceneComponent* Parent, FName SocketName, EAttachmentRule LocationRule, EAttachmentRule RotationRule, EAttachmentRule ScaleRule, bool bWeldSimulatedBodies);
    
    UFUNCTION(BlueprintCallable)
    bool K2_AttachTo(USceneComponent* InParent, FName InSocketName, TEnumAsByte<EAttachLocation::Type> AttachType, bool bWeldSimulatedBodies);
    
    UFUNCTION(BlueprintCallable)
    void K2_AddWorldTransform(const FTransform& DeltaTransform, bool bSweep, FHitResult& SweepHitResult, bool bTeleport);
    
    UFUNCTION(BlueprintCallable)
    void K2_AddWorldRotation(FRotator DeltaRotation, bool bSweep, FHitResult& SweepHitResult, bool bTeleport);
    
    UFUNCTION(BlueprintCallable)
    void K2_AddWorldOffset(FVector DeltaLocation, bool bSweep, FHitResult& SweepHitResult, bool bTeleport);
    
    UFUNCTION(BlueprintCallable)
    void K2_AddRelativeRotation(FRotator DeltaRotation, bool bSweep, FHitResult& SweepHitResult, bool bTeleport);
    
    UFUNCTION(BlueprintCallable)
    void K2_AddRelativeLocation(FVector DeltaLocation, bool bSweep, FHitResult& SweepHitResult, bool bTeleport);
    
    UFUNCTION(BlueprintCallable)
    void K2_AddLocalTransform(const FTransform& DeltaTransform, bool bSweep, FHitResult& SweepHitResult, bool bTeleport);
    
    UFUNCTION(BlueprintCallable)
    void K2_AddLocalRotation(FRotator DeltaRotation, bool bSweep, FHitResult& SweepHitResult, bool bTeleport);
    
    UFUNCTION(BlueprintCallable)
    void K2_AddLocalOffset(FVector DeltaLocation, bool bSweep, FHitResult& SweepHitResult, bool bTeleport);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool IsVisible() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool IsSimulatingPhysics(FName BoneName) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool IsAnySimulatingPhysics() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FVector GetUpVector() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FTransform GetSocketTransform(FName InSocketName, TEnumAsByte<ERelativeTransformSpace> TransformSpace) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FRotator GetSocketRotation(FName InSocketName) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FQuat GetSocketQuaternion(FName InSocketName) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FVector GetSocketLocation(FName InSocketName) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FVector GetRightVector() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FTransform GetRelativeTransform() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    APhysicsVolume* GetPhysicsVolume() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    void GetParentComponents(TArray<USceneComponent*>& Parents) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    int32 GetNumChildrenComponents() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FVector GetForwardVector() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FVector GetComponentVelocity() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    void GetChildrenComponents(bool bIncludeAllDescendants, TArray<USceneComponent*>& Children) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    USceneComponent* GetChildComponent(int32 ChildIndex) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FName GetAttachSocketName() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    USceneComponent* GetAttachParent() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    TArray<FName> GetAllSocketNames() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool DoesSocketExist(FName InSocketName) const;
    
    UFUNCTION(BlueprintCallable)
    void DetachFromParent(bool bMaintainWorldPosition, bool bCallModify);
    
};

