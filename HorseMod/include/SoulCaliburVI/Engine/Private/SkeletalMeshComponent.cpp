#include "SkeletalMeshComponent.h"

USkeletalMeshComponent::USkeletalMeshComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->bGenerateOverlapEvents = false;
    this->bHasCustomNavigableGeometry = EHasCustomNavigableGeometry::Yes;
    this->AnimationMode = EAnimationMode::AnimationBlueprint;
    this->AnimBlueprintGeneratedClass = NULL;
    this->AnimClass = NULL;
    this->AnimScriptInstance = NULL;
    this->PostProcessAnimInstance = NULL;
    this->GlobalAnimRateScale = 1.00f;
    this->UseAsyncScene = EDynamicActorScene::Default;
    this->bHasValidBodies = false;
    this->KinematicBonesUpdateType = EKinematicBonesUpdateToPhysics::SkipSimulatingBones;
    this->PhysicsTransformUpdateMode = EPhysicsTransformUpdateMode::SimulationUpatesComponentTransform;
    this->bBlendPhysics = false;
    this->bEnablePhysicsOnDedicatedServer = true;
    this->bUpdateJointsFromAnimation = false;
    this->bDisableClothSimulation = false;
    this->bDisableAnimCurves = false;
    this->bCollideWithEnvironment = false;
    this->bCollideWithAttachedChildren = false;
    this->bLocalSpaceSimulation = false;
    this->bClothMorphTarget = false;
    this->bResetAfterTeleport = true;
    this->ClothBlendWeight = 1.00f;
    this->bDeferMovementFromSceneQueries = false;
    this->bNoSkeletonUpdate = false;
    this->bPauseAnims = false;
    this->bUseRefPoseOnInitAnim = false;
    this->bEnablePerPolyCollision = false;
    this->BodySetup = NULL;
    this->bOnlyAllowAutonomousTickPose = false;
    this->bIsAutonomousTickPose = false;
    this->bForceRefpose = false;
    this->bOldForceRefPose = false;
    this->bShowPrePhysBones = false;
    this->bRequiredBonesUpToDate = false;
    this->bAnimTreeInitialised = false;
    this->bIncludeComponentLocationIntoBounds = false;
    this->bEnableLineCheckWithBounds = false;
    this->CachedAnimCurveUidVersion = 0;
    this->ClothingSimulationFactory = NULL;
    this->TeleportDistanceThreshold = 300.00f;
    this->TeleportRotationThreshold = 0.00f;
    this->SequenceToPlay = NULL;
    this->AnimToPlay = NULL;
    this->bDefaultLooping = false;
    this->bDefaultPlaying = true;
    this->DefaultPosition = 0.00f;
    this->DefaultPlayRate = 1.00f;
    this->LastPoseTickFrame = 0;
    this->LastPoseTickTime = 0.00f;
    this->bNeedsQueuedAnimEventsDispatched = false;
}

void USkeletalMeshComponent::UnbindClothFromMasterPoseComponent(bool bRestoreSimulationSpace) {
}

void USkeletalMeshComponent::SuspendClothingSimulation() {
}

void USkeletalMeshComponent::Stop() {
}

void USkeletalMeshComponent::SnapshotPose(FPoseSnapshot& Snapshot) {
}

void USkeletalMeshComponent::SetUpdateAnimationInEditor(const bool NewUpdateState) {
}

void USkeletalMeshComponent::SetTeleportRotationThreshold(float Threshold) {
}

void USkeletalMeshComponent::SetTeleportDistanceThreshold(float Threshold) {
}

void USkeletalMeshComponent::SetPosition(float InPos, bool bFireNotifies) {
}

void USkeletalMeshComponent::SetPlayRate(float rate) {
}

void USkeletalMeshComponent::SetPhysicsBlendWeight(float PhysicsBlendWeight) {
}

void USkeletalMeshComponent::SetNotifyRigidBodyCollisionBelow(bool bNewNotifyRigidBodyCollision, FName BoneName, bool bIncludeSelf) {
}

void USkeletalMeshComponent::SetMorphTarget(FName MorphTargetName, float Value, bool bRemoveZeroWeight) {
}

void USkeletalMeshComponent::SetEnablePhysicsBlending(bool bNewBlendPhysics) {
}

void USkeletalMeshComponent::SetEnableGravityOnAllBodiesBelow(bool bEnableGravity, FName BoneName, bool bIncludeSelf) {
}

void USkeletalMeshComponent::SetEnableBodyGravity(bool bEnableGravity, FName BoneName) {
}

void USkeletalMeshComponent::SetDisableAnimCurves(bool bInDisableAnimCurves) {
}

void USkeletalMeshComponent::SetConstraintProfileForAll(FName ProfileName, bool bDefaultIfNotFound) {
}

void USkeletalMeshComponent::SetConstraintProfile(FName JointName, FName ProfileName, bool bDefaultIfNotFound) {
}

void USkeletalMeshComponent::SetClothMaxDistanceScale(float Scale) {
}

void USkeletalMeshComponent::SetBodyNotifyRigidBodyCollision(bool bNewNotifyRigidBodyCollision, FName BoneName) {
}

void USkeletalMeshComponent::SetAnimInstanceClass(UClass* NewClass) {
}

void USkeletalMeshComponent::SetAnimationMode(TEnumAsByte<EAnimationMode::Type> InAnimationMode) {
}

void USkeletalMeshComponent::SetAnimation(UAnimationAsset* NewAnimToPlay) {
}

void USkeletalMeshComponent::SetAngularLimits(FName InBoneName, float Swing1LimitAngle, float TwistLimitAngle, float Swing2LimitAngle) {
}

void USkeletalMeshComponent::SetAllMotorsAngularVelocityDrive(bool bEnableSwingDrive, bool bEnableTwistDrive, bool bSkipCustomPhysicsType) {
}

void USkeletalMeshComponent::SetAllMotorsAngularPositionDrive(bool bEnableSwingDrive, bool bEnableTwistDrive, bool bSkipCustomPhysicsType) {
}

void USkeletalMeshComponent::SetAllMotorsAngularDriveParams(float InSpring, float InDamping, float InForceLimit, bool bSkipCustomPhysicsType) {
}

void USkeletalMeshComponent::SetAllBodiesSimulatePhysics(bool bNewSimulate) {
}

void USkeletalMeshComponent::SetAllBodiesPhysicsBlendWeight(float PhysicsBlendWeight, bool bSkipCustomPhysicsType) {
}

void USkeletalMeshComponent::SetAllBodiesBelowSimulatePhysics(const FName& InBoneName, bool bNewSimulate, bool bIncludeSelf) {
}

void USkeletalMeshComponent::SetAllBodiesBelowPhysicsBlendWeight(const FName& InBoneName, float PhysicsBlendWeight, bool bSkipCustomPhysicsType, bool bIncludeSelf) {
}

void USkeletalMeshComponent::ResumeClothingSimulation() {
}

void USkeletalMeshComponent::ResetClothTeleportMode() {
}

void USkeletalMeshComponent::ResetAllBodiesSimulatePhysics() {
}

void USkeletalMeshComponent::PlayAnimation(UAnimationAsset* NewAnimToPlay, bool bLooping) {
}

void USkeletalMeshComponent::Play(bool bLooping) {
}

void USkeletalMeshComponent::OverrideAnimationData(UAnimationAsset* InAnimToPlay, bool bIsLooping, bool bIsPlaying, float Position, float PlayRate) {
}

bool USkeletalMeshComponent::K2_GetClosestPointOnPhysicsAsset(const FVector& WorldPosition, FVector& ClosestWorldPosition, FVector& Normal, FName& BoneName, float& Distance) const {
    return false;
}

bool USkeletalMeshComponent::IsPlaying() const {
    return false;
}

bool USkeletalMeshComponent::IsClothingSimulationSuspended() {
    return false;
}

bool USkeletalMeshComponent::IsBodyGravityEnabled(FName BoneName) {
    return false;
}

float USkeletalMeshComponent::GetTeleportRotationThreshold() const {
    return 0.0f;
}

float USkeletalMeshComponent::GetTeleportDistanceThreshold() const {
    return 0.0f;
}

FVector USkeletalMeshComponent::GetSkeletalCenterOfMass() const {
    return FVector{};
}

UAnimInstance* USkeletalMeshComponent::GetPostProcessInstance() const {
    return NULL;
}

float USkeletalMeshComponent::GetPosition() const {
    return 0.0f;
}

float USkeletalMeshComponent::GetPlayRate() const {
    return 0.0f;
}

float USkeletalMeshComponent::GetMorphTarget(FName MorphTargetName) const {
    return 0.0f;
}

bool USkeletalMeshComponent::GetDisableAnimCurves() const {
    return false;
}

void USkeletalMeshComponent::GetCurrentJointAngles(FName InBoneName, float& Swing1Angle, float& TwistAngle, float& Swing2Angle) {
}

float USkeletalMeshComponent::GetClothMaxDistanceScale() {
    return 0.0f;
}

float USkeletalMeshComponent::GetBoneMass(FName BoneName, bool bScaleMass) const {
    return 0.0f;
}

UAnimInstance* USkeletalMeshComponent::GetAnimInstance() const {
    return NULL;
}

TEnumAsByte<EAnimationMode::Type> USkeletalMeshComponent::GetAnimationMode() const {
    return EAnimationMode::AnimationBlueprint;
}

void USkeletalMeshComponent::ForceClothNextUpdateTeleportAndReset() {
}

void USkeletalMeshComponent::ForceClothNextUpdateTeleport() {
}

FName USkeletalMeshComponent::FindConstraintBoneName(int32 ConstraintIndex) {
    return NAME_None;
}

void USkeletalMeshComponent::ClearMorphTargets() {
}

void USkeletalMeshComponent::BreakConstraint(FVector Impulse, FVector HitLocation, FName InBoneName) {
}

void USkeletalMeshComponent::BindClothToMasterPoseComponent() {
}

void USkeletalMeshComponent::AddImpulseToAllBodiesBelow(FVector Impulse, FName BoneName, bool bVelChange, bool bIncludeSelf) {
}

void USkeletalMeshComponent::AddForceToAllBodiesBelow(FVector Force, FName BoneName, bool bAccelChange, bool bIncludeSelf) {
}

void USkeletalMeshComponent::AccumulateAllBodiesBelowPhysicsBlendWeight(const FName& InBoneName, float AddPhysicsBlendWeight, bool bSkipCustomPhysicsType) {
}


