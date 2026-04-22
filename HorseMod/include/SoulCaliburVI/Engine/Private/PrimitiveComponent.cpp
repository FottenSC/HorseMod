#include "PrimitiveComponent.h"
#include "Templates/SubclassOf.h"

UPrimitiveComponent::UPrimitiveComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->MinDrawDistance = 0.00f;
    this->LDMaxDrawDistance = 0.00f;
    this->CachedMaxDrawDistance = 0.00f;
    this->DepthPriorityGroup = SDPG_World;
    this->ViewOwnerDepthPriorityGroup = SDPG_World;
    this->bAlwaysCreatePhysicsState = false;
    this->bGenerateOverlapEvents = true;
    this->bMultiBodyOverlap = false;
    this->bCheckAsyncSceneOnMove = false;
    this->bTraceComplexOnMove = false;
    this->bReturnMaterialOnMove = false;
    this->bUseViewOwnerDepthPriorityGroup = false;
    this->bAllowCullDistanceVolume = true;
    this->bHasMotionBlurVelocityMeshes = false;
    this->bVisibleInReflectionCaptures = true;
    this->bRenderInMainPass = true;
    this->bRenderInMono = false;
    this->bReceivesDecals = true;
    this->bOwnerNoSee = false;
    this->bOnlyOwnerSee = false;
    this->bTreatAsBackgroundForOcclusion = false;
    this->bUseAsOccluder = false;
    this->bSelectable = true;
    this->bForceMipStreaming = false;
    this->bHasPerInstanceHitProxies = false;
    this->CastShadow = false;
    this->bAffectDynamicIndirectLighting = true;
    this->bAffectDistanceFieldLighting = true;
    this->bCastDynamicShadow = true;
    this->bCastStaticShadow = true;
    this->bCastVolumetricTranslucentShadow = false;
    this->bSelfShadowOnly = false;
    this->bCastFarShadow = false;
    this->bCastInsetShadow = false;
    this->bCastCinematicShadow = false;
    this->bCastHiddenShadow = false;
    this->bCastShadowAsTwoSided = false;
    this->bLightAsIfStatic = false;
    this->bLightAttachmentsAsGroup = false;
    this->IndirectLightingCacheQuality = ILCQ_Point;
    this->bReceiveCombinedCSMAndStaticShadowsFromStationaryLights = false;
    this->bSingleSampleShadowFromStationaryLights = false;
    this->bIgnoreRadialImpulse = false;
    this->bIgnoreRadialForce = false;
    this->bApplyImpulseOnDamage = true;
    this->AlwaysLoadOnClient = true;
    this->AlwaysLoadOnServer = true;
    this->bUseEditorCompositing = false;
    this->bRenderCustomDepth = false;
    this->CustomDepthStencilValue = 0;
    this->CustomDepthStencilWriteMask = ERendererStencilMask::ERSM_Default;
    this->TranslucencySortPriority = 0;
    this->VisibilityId = -1;
    this->LpvBiasMultiplier = 1.00f;
    this->bHasCustomNavigableGeometry = EHasCustomNavigableGeometry::No;
    this->BoundsScale = 1.00f;
    this->LastSubmitTime = 0.00f;
    this->LastRenderTime = -1000.00f;
    this->LastRenderTimeOnScreen = -1000.00f;
    this->CanBeCharacterBase = ECB_Yes;
    this->CanCharacterStepUpOn = ECB_Yes;
    this->LODParentPrimitive = NULL;
}

void UPrimitiveComponent::WakeRigidBody(FName BoneName) {
}

void UPrimitiveComponent::WakeAllRigidBodies() {
}

void UPrimitiveComponent::SetWalkableSlopeOverride(const FWalkableSlopeOverride& NewOverride) {
}

void UPrimitiveComponent::SetTranslucentSortPriority(int32 NewTranslucentSortPriority) {
}

void UPrimitiveComponent::SetSingleSampleShadowFromStationaryLights(bool bNewSingleSampleShadowFromStationaryLights) {
}

void UPrimitiveComponent::SetSimulatePhysics(bool bSimulate) {
}

void UPrimitiveComponent::SetRenderInMono(bool bValue) {
}

void UPrimitiveComponent::SetRenderInMainPass(bool bValue) {
}

void UPrimitiveComponent::SetRenderCustomDepth(bool bValue) {
}

void UPrimitiveComponent::SetReceivesDecals(bool bNewReceivesDecals) {
}

void UPrimitiveComponent::SetPhysMaterialOverride(UPhysicalMaterial* NewPhysMaterial) {
}

void UPrimitiveComponent::SetPhysicsMaxAngularVelocity(float NewMaxAngVel, bool bAddToCurrent, FName BoneName) {
}

void UPrimitiveComponent::SetPhysicsLinearVelocity(FVector NewVel, bool bAddToCurrent, FName BoneName) {
}

void UPrimitiveComponent::SetPhysicsAngularVelocity(FVector NewAngVel, bool bAddToCurrent, FName BoneName) {
}

void UPrimitiveComponent::SetOwnerNoSee(bool bNewOwnerNoSee) {
}

void UPrimitiveComponent::SetOnlyOwnerSee(bool bNewOnlyOwnerSee) {
}

void UPrimitiveComponent::SetNotifyRigidBodyCollision(bool bNewNotifyRigidBodyCollision) {
}

void UPrimitiveComponent::SetMaterialByName(FName MaterialSlotName, UMaterialInterface* Material) {
}

void UPrimitiveComponent::SetMaterial(int32 ElementIndex, UMaterialInterface* Material) {
}

void UPrimitiveComponent::SetMassScale(FName BoneName, float InMassScale) {
}

void UPrimitiveComponent::SetMassOverrideInKg(FName BoneName, float MassInKg, bool bOverrideMass) {
}

void UPrimitiveComponent::SetLockedAxis(TEnumAsByte<EDOFMode::Type> LockedAxis) {
}

void UPrimitiveComponent::SetLinearDamping(float InDamping) {
}

void UPrimitiveComponent::SetEnableGravity(bool bGravityEnabled) {
}

void UPrimitiveComponent::SetCustomDepthStencilWriteMask(ERendererStencilMask WriteMaskBit) {
}

void UPrimitiveComponent::SetCustomDepthStencilValue(int32 Value) {
}

void UPrimitiveComponent::SetCullDistance(float NewCullDistance) {
}

void UPrimitiveComponent::SetConstraintMode(TEnumAsByte<EDOFMode::Type> ConstraintMode) {
}

void UPrimitiveComponent::SetCollisionResponseToChannel(TEnumAsByte<ECollisionChannel> Channel, TEnumAsByte<ECollisionResponse> NewResponse) {
}

void UPrimitiveComponent::SetCollisionResponseToAllChannels(TEnumAsByte<ECollisionResponse> NewResponse) {
}

void UPrimitiveComponent::SetCollisionProfileName(FName InCollisionProfileName) {
}

void UPrimitiveComponent::SetCollisionObjectType(TEnumAsByte<ECollisionChannel> Channel) {
}

void UPrimitiveComponent::SetCollisionEnabled(TEnumAsByte<ECollisionEnabled::Type> NewType) {
}

void UPrimitiveComponent::SetCenterOfMass(FVector CenterOfMassOffset, FName BoneName) {
}

void UPrimitiveComponent::SetCastShadow(bool NewCastShadow) {
}

void UPrimitiveComponent::SetBoundsScale(float NewBoundsScale) {
}

void UPrimitiveComponent::SetAngularDamping(float InDamping) {
}

void UPrimitiveComponent::SetAllPhysicsLinearVelocity(FVector NewVel, bool bAddToCurrent) {
}

void UPrimitiveComponent::SetAllPhysicsAngularVelocity(const FVector& NewAngVel, bool bAddToCurrent) {
}

void UPrimitiveComponent::SetAllMassScale(float InMassScale) {
}

FVector UPrimitiveComponent::ScaleByMomentOfInertia(FVector InputVector, FName BoneName) const {
    return FVector{};
}

void UPrimitiveComponent::PutRigidBodyToSleep(FName BoneName) {
}

bool UPrimitiveComponent::K2_LineTraceComponent(FVector TraceStart, FVector TraceEnd, bool bTraceComplex, bool bShowTrace, FVector& HitLocation, FVector& HitNormal, FName& BoneName, FHitResult& OutHit) {
    return false;
}

bool UPrimitiveComponent::K2_IsQueryCollisionEnabled() const {
    return false;
}

bool UPrimitiveComponent::K2_IsPhysicsCollisionEnabled() const {
    return false;
}

bool UPrimitiveComponent::K2_IsCollisionEnabled() const {
    return false;
}

bool UPrimitiveComponent::IsOverlappingComponent(const UPrimitiveComponent* OtherComp) const {
    return false;
}

bool UPrimitiveComponent::IsOverlappingActor(const AActor* Other) const {
    return false;
}

bool UPrimitiveComponent::IsGravityEnabled() const {
    return false;
}

bool UPrimitiveComponent::IsAnyRigidBodyAwake() {
    return false;
}

void UPrimitiveComponent::IgnoreComponentWhenMoving(UPrimitiveComponent* Component, bool bShouldIgnore) {
}

void UPrimitiveComponent::IgnoreActorWhenMoving(AActor* Actor, bool bShouldIgnore) {
}

FWalkableSlopeOverride UPrimitiveComponent::GetWalkableSlopeOverride() const {
    return FWalkableSlopeOverride{};
}

FVector UPrimitiveComponent::GetPhysicsLinearVelocityAtPoint(FVector Point, FName BoneName) {
    return FVector{};
}

FVector UPrimitiveComponent::GetPhysicsLinearVelocity(FName BoneName) {
    return FVector{};
}

FVector UPrimitiveComponent::GetPhysicsAngularVelocity(FName BoneName) {
    return FVector{};
}

void UPrimitiveComponent::GetOverlappingComponents(TArray<UPrimitiveComponent*>& InOverlappingComponents) const {
}

void UPrimitiveComponent::GetOverlappingActors(TArray<AActor*>& OverlappingActors, TSubclassOf<AActor> ClassFilter) const {
}

int32 UPrimitiveComponent::GetNumMaterials() const {
    return 0;
}

UMaterialInterface* UPrimitiveComponent::GetMaterialFromCollisionFaceIndex(int32 FaceIndex, int32& SectionIndex) const {
    return NULL;
}

UMaterialInterface* UPrimitiveComponent::GetMaterial(int32 ElementIndex) const {
    return NULL;
}

float UPrimitiveComponent::GetMassScale(FName BoneName) const {
    return 0.0f;
}

float UPrimitiveComponent::GetMass() const {
    return 0.0f;
}

float UPrimitiveComponent::GetLinearDamping() const {
    return 0.0f;
}

FVector UPrimitiveComponent::GetInertiaTensor(FName BoneName) const {
    return FVector{};
}

TEnumAsByte<ECollisionResponse> UPrimitiveComponent::GetCollisionResponseToChannel(TEnumAsByte<ECollisionChannel> Channel) const {
    return ECR_Ignore;
}

FName UPrimitiveComponent::GetCollisionProfileName() const {
    return NAME_None;
}

TEnumAsByte<ECollisionChannel> UPrimitiveComponent::GetCollisionObjectType() const {
    return ECC_WorldStatic;
}

TEnumAsByte<ECollisionEnabled::Type> UPrimitiveComponent::GetCollisionEnabled() const {
    return ECollisionEnabled::NoCollision;
}

float UPrimitiveComponent::GetClosestPointOnCollision(const FVector& Point, FVector& OutPointOnBody, FName BoneName) const {
    return 0.0f;
}

FVector UPrimitiveComponent::GetCenterOfMass(FName BoneName) const {
    return FVector{};
}

float UPrimitiveComponent::GetAngularDamping() const {
    return 0.0f;
}

UMaterialInstanceDynamic* UPrimitiveComponent::CreateDynamicMaterialInstance(int32 ElementIndex, UMaterialInterface* SourceMaterial) {
    return NULL;
}

UMaterialInstanceDynamic* UPrimitiveComponent::CreateAndSetMaterialInstanceDynamicFromMaterial(int32 ElementIndex, UMaterialInterface* Parent) {
    return NULL;
}

UMaterialInstanceDynamic* UPrimitiveComponent::CreateAndSetMaterialInstanceDynamic(int32 ElementIndex) {
    return NULL;
}

TArray<UPrimitiveComponent*> UPrimitiveComponent::CopyArrayOfMoveIgnoreComponents() {
    return TArray<UPrimitiveComponent*>();
}

TArray<AActor*> UPrimitiveComponent::CopyArrayOfMoveIgnoreActors() {
    return TArray<AActor*>();
}

void UPrimitiveComponent::ClearMoveIgnoreComponents() {
}

void UPrimitiveComponent::ClearMoveIgnoreActors() {
}

bool UPrimitiveComponent::CanCharacterStepUp(APawn* Pawn) const {
    return false;
}

void UPrimitiveComponent::AddTorque(FVector Torque, FName BoneName, bool bAccelChange) {
}

void UPrimitiveComponent::AddRadialImpulse(FVector Origin, float Radius, float Strength, TEnumAsByte<ERadialImpulseFalloff> Falloff, bool bVelChange) {
}

void UPrimitiveComponent::AddRadialForce(FVector Origin, float Radius, float Strength, TEnumAsByte<ERadialImpulseFalloff> Falloff, bool bAccelChange) {
}

void UPrimitiveComponent::AddImpulseAtLocation(FVector Impulse, FVector Location, FName BoneName) {
}

void UPrimitiveComponent::AddImpulse(FVector Impulse, FName BoneName, bool bVelChange) {
}

void UPrimitiveComponent::AddForceAtLocationLocal(FVector Force, FVector Location, FName BoneName) {
}

void UPrimitiveComponent::AddForceAtLocation(FVector Force, FVector Location, FName BoneName) {
}

void UPrimitiveComponent::AddForce(FVector Force, FName BoneName, bool bAccelChange) {
}

void UPrimitiveComponent::AddAngularImpulse(FVector Impulse, FName BoneName, bool bVelChange) {
}


