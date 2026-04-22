#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Vector -FallbackName=Vector
#include "BodyInstance.h"
#include "ComponentBeginCursorOverSignatureDelegate.h"
#include "ComponentBeginOverlapSignatureDelegate.h"
#include "ComponentBeginTouchOverSignatureDelegate.h"
#include "ComponentEndCursorOverSignatureDelegate.h"
#include "ComponentEndOverlapSignatureDelegate.h"
#include "ComponentEndTouchOverSignatureDelegate.h"
#include "ComponentHitSignatureDelegate.h"
#include "ComponentOnClickedSignatureDelegate.h"
#include "ComponentOnInputTouchBeginSignatureDelegate.h"
#include "ComponentOnInputTouchEndSignatureDelegate.h"
#include "ComponentOnReleasedSignatureDelegate.h"
#include "ComponentSleepSignatureDelegate.h"
#include "ComponentWakeSignatureDelegate.h"
#include "ECanBeCharacterBase.h"
#include "ECollisionChannel.h"
#include "ECollisionEnabled.h"
#include "ECollisionResponse.h"
#include "EDOFMode.h"
#include "EHasCustomNavigableGeometry.h"
#include "EIndirectLightingCacheQuality.h"
#include "ERadialImpulseFalloff.h"
#include "ERendererStencilMask.h"
#include "ESceneDepthPriorityGroup.h"
#include "HitResult.h"
#include "LightingChannels.h"
#include "NavRelevantInterface.h"
#include "PrimitiveComponentPostPhysicsTickFunction.h"
#include "SceneComponent.h"
#include "Templates/SubclassOf.h"
#include "WalkableSlopeOverride.h"
#include "PrimitiveComponent.generated.h"

class AActor;
class APawn;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPhysicalMaterial;
class UPrimitiveComponent;

UCLASS(Abstract, Blueprintable, ClassGroup=Custom, meta=(BlueprintSpawnableComponent))
class ENGINE_API UPrimitiveComponent : public USceneComponent, public INavRelevantInterface {
    GENERATED_BODY()
public:
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float MinDrawDistance;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float LDMaxDrawDistance;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float CachedMaxDrawDistance;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TEnumAsByte<ESceneDepthPriorityGroup> DepthPriorityGroup;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TEnumAsByte<ESceneDepthPriorityGroup> ViewOwnerDepthPriorityGroup;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bAlwaysCreatePhysicsState: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bGenerateOverlapEvents: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bMultiBodyOverlap: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bCheckAsyncSceneOnMove: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bTraceComplexOnMove: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bReturnMaterialOnMove: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bUseViewOwnerDepthPriorityGroup: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bAllowCullDistanceVolume: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bHasMotionBlurVelocityMeshes: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bVisibleInReflectionCaptures: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bRenderInMainPass: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bRenderInMono: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bReceivesDecals: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bOwnerNoSee: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bOnlyOwnerSee: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bTreatAsBackgroundForOcclusion: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bUseAsOccluder: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bSelectable: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bForceMipStreaming: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bHasPerInstanceHitProxies: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 CastShadow: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bAffectDynamicIndirectLighting: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bAffectDistanceFieldLighting: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bCastDynamicShadow: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bCastStaticShadow: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bCastVolumetricTranslucentShadow: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bSelfShadowOnly: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bCastFarShadow: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bCastInsetShadow: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bCastCinematicShadow: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bCastHiddenShadow: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bCastShadowAsTwoSided: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bLightAsIfStatic: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bLightAttachmentsAsGroup: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TEnumAsByte<EIndirectLightingCacheQuality> IndirectLightingCacheQuality;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bReceiveCombinedCSMAndStaticShadowsFromStationaryLights: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bSingleSampleShadowFromStationaryLights: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FLightingChannels LightingChannels;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bIgnoreRadialImpulse: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bIgnoreRadialForce: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bApplyImpulseOnDamage: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 AlwaysLoadOnClient: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 AlwaysLoadOnServer: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bUseEditorCompositing: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bRenderCustomDepth: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    int32 CustomDepthStencilValue;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    ERendererStencilMask CustomDepthStencilWriteMask;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    int32 TranslucencySortPriority;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    int32 VisibilityId;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float LpvBiasMultiplier;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FBodyInstance BodyInstance;
    
protected:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TEnumAsByte<EHasCustomNavigableGeometry::Type> bHasCustomNavigableGeometry;
    
public:
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float BoundsScale;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    float LastSubmitTime;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    float LastRenderTime;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    float LastRenderTimeOnScreen;
    
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TEnumAsByte<ECanBeCharacterBase> CanBeCharacterBase;
    
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TEnumAsByte<ECanBeCharacterBase> CanCharacterStepUpOn;
    
    UPROPERTY(BlueprintReadWrite, DuplicateTransient, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    TArray<AActor*> MoveIgnoreActors;
    
    UPROPERTY(BlueprintReadWrite, DuplicateTransient, EditAnywhere, Instanced, Transient, meta=(AllowPrivateAccess=true))
    TArray<UPrimitiveComponent*> MoveIgnoreComponents;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FComponentHitSignature OnComponentHit;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FComponentBeginOverlapSignature OnComponentBeginOverlap;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FComponentEndOverlapSignature OnComponentEndOverlap;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FComponentWakeSignature OnComponentWake;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FComponentSleepSignature OnComponentSleep;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FComponentBeginCursorOverSignature OnBeginCursorOver;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FComponentEndCursorOverSignature OnEndCursorOver;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FComponentOnClickedSignature OnClicked;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FComponentOnReleasedSignature OnReleased;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FComponentOnInputTouchBeginSignature OnInputTouchBegin;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FComponentOnInputTouchEndSignature OnInputTouchEnd;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FComponentBeginTouchOverSignature OnInputTouchEnter;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FComponentEndTouchOverSignature OnInputTouchLeave;
    
private:
    UPROPERTY(BlueprintReadWrite, DuplicateTransient, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    UPrimitiveComponent* LODParentPrimitive;
    
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FPrimitiveComponentPostPhysicsTickFunction PostPhysicsComponentTick;
    
    UPrimitiveComponent(const FObjectInitializer& ObjectInitializer);

    UFUNCTION(BlueprintCallable)
    void WakeRigidBody(FName BoneName);
    
    UFUNCTION(BlueprintCallable)
    void WakeAllRigidBodies();
    
    UFUNCTION(BlueprintCallable)
    void SetWalkableSlopeOverride(const FWalkableSlopeOverride& NewOverride);
    
    UFUNCTION(BlueprintCallable)
    void SetTranslucentSortPriority(int32 NewTranslucentSortPriority);
    
    UFUNCTION(BlueprintCallable)
    void SetSingleSampleShadowFromStationaryLights(bool bNewSingleSampleShadowFromStationaryLights);
    
    UFUNCTION(BlueprintCallable)
    void SetSimulatePhysics(bool bSimulate);
    
    UFUNCTION(BlueprintCallable)
    void SetRenderInMono(bool bValue);
    
    UFUNCTION(BlueprintCallable)
    void SetRenderInMainPass(bool bValue);
    
    UFUNCTION(BlueprintCallable)
    void SetRenderCustomDepth(bool bValue);
    
    UFUNCTION(BlueprintCallable)
    void SetReceivesDecals(bool bNewReceivesDecals);
    
    UFUNCTION(BlueprintCallable)
    void SetPhysMaterialOverride(UPhysicalMaterial* NewPhysMaterial);
    
    UFUNCTION(BlueprintCallable)
    void SetPhysicsMaxAngularVelocity(float NewMaxAngVel, bool bAddToCurrent, FName BoneName);
    
    UFUNCTION(BlueprintCallable)
    void SetPhysicsLinearVelocity(FVector NewVel, bool bAddToCurrent, FName BoneName);
    
    UFUNCTION(BlueprintCallable)
    void SetPhysicsAngularVelocity(FVector NewAngVel, bool bAddToCurrent, FName BoneName);
    
    UFUNCTION(BlueprintCallable)
    void SetOwnerNoSee(bool bNewOwnerNoSee);
    
    UFUNCTION(BlueprintCallable)
    void SetOnlyOwnerSee(bool bNewOnlyOwnerSee);
    
    UFUNCTION(BlueprintCallable)
    void SetNotifyRigidBodyCollision(bool bNewNotifyRigidBodyCollision);
    
    UFUNCTION(BlueprintCallable)
    void SetMaterialByName(FName MaterialSlotName, UMaterialInterface* Material);
    
    UFUNCTION(BlueprintCallable)
    void SetMaterial(int32 ElementIndex, UMaterialInterface* Material);
    
    UFUNCTION(BlueprintCallable)
    void SetMassScale(FName BoneName, float InMassScale);
    
    UFUNCTION(BlueprintCallable)
    void SetMassOverrideInKg(FName BoneName, float MassInKg, bool bOverrideMass);
    
    UFUNCTION(BlueprintCallable)
    void SetLockedAxis(TEnumAsByte<EDOFMode::Type> LockedAxis);
    
    UFUNCTION(BlueprintCallable)
    void SetLinearDamping(float InDamping);
    
    UFUNCTION(BlueprintCallable)
    void SetEnableGravity(bool bGravityEnabled);
    
    UFUNCTION(BlueprintCallable)
    void SetCustomDepthStencilWriteMask(ERendererStencilMask WriteMaskBit);
    
    UFUNCTION(BlueprintCallable)
    void SetCustomDepthStencilValue(int32 Value);
    
    UFUNCTION(BlueprintCallable)
    void SetCullDistance(float NewCullDistance);
    
    UFUNCTION(BlueprintCallable)
    void SetConstraintMode(TEnumAsByte<EDOFMode::Type> ConstraintMode);
    
    UFUNCTION(BlueprintCallable)
    void SetCollisionResponseToChannel(TEnumAsByte<ECollisionChannel> Channel, TEnumAsByte<ECollisionResponse> NewResponse);
    
    UFUNCTION(BlueprintCallable)
    void SetCollisionResponseToAllChannels(TEnumAsByte<ECollisionResponse> NewResponse);
    
    UFUNCTION(BlueprintCallable)
    void SetCollisionProfileName(FName InCollisionProfileName);
    
    UFUNCTION(BlueprintCallable)
    void SetCollisionObjectType(TEnumAsByte<ECollisionChannel> Channel);
    
    UFUNCTION(BlueprintCallable)
    void SetCollisionEnabled(TEnumAsByte<ECollisionEnabled::Type> NewType);
    
    UFUNCTION(BlueprintCallable)
    void SetCenterOfMass(FVector CenterOfMassOffset, FName BoneName);
    
    UFUNCTION(BlueprintCallable)
    void SetCastShadow(bool NewCastShadow);
    
    UFUNCTION(BlueprintCallable)
    void SetBoundsScale(float NewBoundsScale);
    
    UFUNCTION(BlueprintCallable)
    void SetAngularDamping(float InDamping);
    
    UFUNCTION(BlueprintCallable)
    void SetAllPhysicsLinearVelocity(FVector NewVel, bool bAddToCurrent);
    
    UFUNCTION(BlueprintCallable)
    void SetAllPhysicsAngularVelocity(const FVector& NewAngVel, bool bAddToCurrent);
    
    UFUNCTION(BlueprintCallable)
    void SetAllMassScale(float InMassScale);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FVector ScaleByMomentOfInertia(FVector InputVector, FName BoneName) const;
    
    UFUNCTION(BlueprintCallable)
    void PutRigidBodyToSleep(FName BoneName);
    
    UFUNCTION(BlueprintCallable)
    bool K2_LineTraceComponent(FVector TraceStart, FVector TraceEnd, bool bTraceComplex, bool bShowTrace, FVector& HitLocation, FVector& HitNormal, FName& BoneName, FHitResult& OutHit);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool K2_IsQueryCollisionEnabled() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool K2_IsPhysicsCollisionEnabled() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool K2_IsCollisionEnabled() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool IsOverlappingComponent(const UPrimitiveComponent* OtherComp) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool IsOverlappingActor(const AActor* Other) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool IsGravityEnabled() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool IsAnyRigidBodyAwake();
    
    UFUNCTION(BlueprintCallable)
    void IgnoreComponentWhenMoving(UPrimitiveComponent* Component, bool bShouldIgnore);
    
    UFUNCTION(BlueprintCallable)
    void IgnoreActorWhenMoving(AActor* Actor, bool bShouldIgnore);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FWalkableSlopeOverride GetWalkableSlopeOverride() const;
    
    UFUNCTION(BlueprintCallable)
    FVector GetPhysicsLinearVelocityAtPoint(FVector Point, FName BoneName);
    
    UFUNCTION(BlueprintCallable)
    FVector GetPhysicsLinearVelocity(FName BoneName);
    
    UFUNCTION(BlueprintCallable)
    FVector GetPhysicsAngularVelocity(FName BoneName);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    void GetOverlappingComponents(TArray<UPrimitiveComponent*>& InOverlappingComponents) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    void GetOverlappingActors(TArray<AActor*>& OverlappingActors, TSubclassOf<AActor> ClassFilter) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    int32 GetNumMaterials() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    UMaterialInterface* GetMaterialFromCollisionFaceIndex(int32 FaceIndex, int32& SectionIndex) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    UMaterialInterface* GetMaterial(int32 ElementIndex) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    float GetMassScale(FName BoneName) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    float GetMass() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    float GetLinearDamping() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FVector GetInertiaTensor(FName BoneName) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    TEnumAsByte<ECollisionResponse> GetCollisionResponseToChannel(TEnumAsByte<ECollisionChannel> Channel) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FName GetCollisionProfileName() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    TEnumAsByte<ECollisionChannel> GetCollisionObjectType() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    TEnumAsByte<ECollisionEnabled::Type> GetCollisionEnabled() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    float GetClosestPointOnCollision(const FVector& Point, FVector& OutPointOnBody, FName BoneName) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FVector GetCenterOfMass(FName BoneName) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    float GetAngularDamping() const;
    
    UFUNCTION(BlueprintCallable)
    UMaterialInstanceDynamic* CreateDynamicMaterialInstance(int32 ElementIndex, UMaterialInterface* SourceMaterial);
    
    UFUNCTION(BlueprintCallable)
    UMaterialInstanceDynamic* CreateAndSetMaterialInstanceDynamicFromMaterial(int32 ElementIndex, UMaterialInterface* Parent);
    
    UFUNCTION(BlueprintCallable)
    UMaterialInstanceDynamic* CreateAndSetMaterialInstanceDynamic(int32 ElementIndex);
    
    UFUNCTION(BlueprintCallable)
    TArray<UPrimitiveComponent*> CopyArrayOfMoveIgnoreComponents();
    
    UFUNCTION(BlueprintCallable)
    TArray<AActor*> CopyArrayOfMoveIgnoreActors();
    
    UFUNCTION(BlueprintCallable)
    void ClearMoveIgnoreComponents();
    
    UFUNCTION(BlueprintCallable)
    void ClearMoveIgnoreActors();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool CanCharacterStepUp(APawn* Pawn) const;
    
    UFUNCTION(BlueprintCallable)
    void AddTorque(FVector Torque, FName BoneName, bool bAccelChange);
    
    UFUNCTION(BlueprintCallable)
    void AddRadialImpulse(FVector Origin, float Radius, float Strength, TEnumAsByte<ERadialImpulseFalloff> Falloff, bool bVelChange);
    
    UFUNCTION(BlueprintCallable)
    void AddRadialForce(FVector Origin, float Radius, float Strength, TEnumAsByte<ERadialImpulseFalloff> Falloff, bool bAccelChange);
    
    UFUNCTION(BlueprintCallable)
    void AddImpulseAtLocation(FVector Impulse, FVector Location, FName BoneName);
    
    UFUNCTION(BlueprintCallable)
    void AddImpulse(FVector Impulse, FName BoneName, bool bVelChange);
    
    UFUNCTION(BlueprintCallable)
    void AddForceAtLocationLocal(FVector Force, FVector Location, FName BoneName);
    
    UFUNCTION(BlueprintCallable)
    void AddForceAtLocation(FVector Force, FVector Location, FName BoneName);
    
    UFUNCTION(BlueprintCallable)
    void AddForce(FVector Force, FName BoneName, bool bAccelChange);
    
    UFUNCTION(BlueprintCallable)
    void AddAngularImpulse(FVector Impulse, FName BoneName, bool bVelChange);
    

    // Fix for true pure virtual functions not being implemented
};

