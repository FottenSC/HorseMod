#include "CharacterMovementComponent.h"

UCharacterMovementComponent::UCharacterMovementComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->CharacterOwner = NULL;
    this->bApplyGravityWhileJumping = true;
    this->GravityScale = 1.00f;
    this->MaxStepHeight = 45.00f;
    this->JumpZVelocity = 420.00f;
    this->JumpOffJumpZFactor = 0.50f;
    this->WalkableFloorAngle = 44.77f;
    this->WalkableFloorZ = 0.71f;
    this->MovementMode = MOVE_None;
    this->CustomMovementMode = 0;
    this->GroundFriction = 8.00f;
    this->MaxWalkSpeed = 600.00f;
    this->MaxWalkSpeedCrouched = 300.00f;
    this->MaxSwimSpeed = 300.00f;
    this->MaxFlySpeed = 600.00f;
    this->MaxCustomMovementSpeed = 600.00f;
    this->MaxAcceleration = 2048.00f;
    this->MinAnalogWalkSpeed = 0.00f;
    this->BrakingFrictionFactor = 2.00f;
    this->BrakingFriction = 0.00f;
    this->bUseSeparateBrakingFriction = false;
    this->BrakingDecelerationWalking = 2048.00f;
    this->BrakingDecelerationFalling = 0.00f;
    this->BrakingDecelerationSwimming = 0.00f;
    this->BrakingDecelerationFlying = 0.00f;
    this->AirControl = 0.05f;
    this->AirControlBoostMultiplier = 2.00f;
    this->AirControlBoostVelocityThreshold = 25.00f;
    this->FallingLateralFriction = 0.00f;
    this->CrouchedHalfHeight = 40.00f;
    this->Buoyancy = 1.00f;
    this->PerchRadiusThreshold = 0.00f;
    this->PerchAdditionalHeight = 40.00f;
    this->bUseControllerDesiredRotation = false;
    this->bOrientRotationToMovement = false;
    this->bSweepWhileNavWalking = true;
    this->bMovementInProgress = false;
    this->bEnableScopedMovementUpdates = true;
    this->bForceMaxAccel = false;
    this->bRunPhysicsWithNoController = false;
    this->bForceNextFloorCheck = true;
    this->bShrinkProxyCapsule = true;
    this->bCanWalkOffLedges = true;
    this->bCanWalkOffLedgesWhenCrouching = false;
    this->bDeferUpdateMoveComponent = false;
    this->DeferredUpdatedMoveComponent = NULL;
    this->MaxOutOfWaterStepHeight = 40.00f;
    this->OutofWaterZ = 420.00f;
    this->Mass = 100.00f;
    this->bEnablePhysicsInteraction = true;
    this->bTouchForceScaledToMass = true;
    this->bPushForceScaledToMass = false;
    this->bPushForceUsingZOffset = false;
    this->bScalePushForceToVelocity = true;
    this->StandingDownwardForceScale = 1.00f;
    this->InitialPushForceFactor = 500.00f;
    this->PushForceFactor = 750000.00f;
    this->PushForcePointZOffsetFactor = -0.75f;
    this->TouchForceFactor = 1.00f;
    this->MinTouchForce = -1.00f;
    this->MaxTouchForce = 250.00f;
    this->RepulsionForce = 2.50f;
    this->bForceBraking = false;
    this->CrouchedSpeedMultiplier = 0.50f;
    this->UpperImpactNormalScale = 0.50f;
    this->ServerLastTransformUpdateTimeStamp = 0.00f;
    this->AnalogInputModifier = 0.00f;
    this->MaxSimulationTimeStep = 0.05f;
    this->MaxSimulationIterations = 8;
    this->MaxDepenetrationWithGeometry = 500.00f;
    this->MaxDepenetrationWithGeometryAsProxy = 100.00f;
    this->MaxDepenetrationWithPawn = 100.00f;
    this->MaxDepenetrationWithPawnAsProxy = 2.00f;
    this->NetworkSimulatedSmoothLocationTime = 0.10f;
    this->NetworkSimulatedSmoothRotationTime = 0.03f;
    this->ListenServerNetworkSimulatedSmoothLocationTime = 0.04f;
    this->ListenServerNetworkSimulatedSmoothRotationTime = 0.03f;
    this->NetProxyShrinkRadius = 0.01f;
    this->NetProxyShrinkHalfHeight = 0.01f;
    this->NetworkMaxSmoothUpdateDistance = 256.00f;
    this->NetworkNoSmoothUpdateDistance = 384.00f;
    this->NetworkSmoothingMode = ENetworkSmoothingMode::Exponential;
    this->LedgeCheckThreshold = 4.00f;
    this->JumpOutOfWaterPitch = 11.25f;
    this->DefaultLandMovementMode = MOVE_Walking;
    this->DefaultWaterMovementMode = MOVE_Swimming;
    this->GroundMovementMode = MOVE_Walking;
    this->bMaintainHorizontalGroundVelocity = true;
    this->bImpartBaseVelocityX = true;
    this->bImpartBaseVelocityY = true;
    this->bImpartBaseVelocityZ = true;
    this->bImpartBaseAngularVelocity = true;
    this->bJustTeleported = true;
    this->bNetworkUpdateReceived = false;
    this->bNetworkMovementModeChanged = false;
    this->bIgnoreClientMovementErrorChecksAndCorrection = false;
    this->bNotifyApex = false;
    this->bCheatFlying = false;
    this->bWantsToCrouch = false;
    this->bCrouchMaintainsBaseLocation = false;
    this->bIgnoreBaseRotation = false;
    this->bFastAttachedMove = false;
    this->bAlwaysCheckFloor = true;
    this->bUseFlatBaseForFloorChecks = false;
    this->bPerformingJumpOff = false;
    this->bWantsToLeaveNavWalking = false;
    this->bUseRVOAvoidance = false;
    this->bRequestedMoveUseAcceleration = true;
    this->bHasRequestedVelocity = false;
    this->bRequestedMoveWithMaxSpeed = false;
    this->bWasAvoidanceUpdated = false;
    this->bProjectNavMeshWalking = false;
    this->bProjectNavMeshOnBothWorldChannels = false;
    this->AvoidanceConsiderationRadius = 500.00f;
    this->AvoidanceUID = 0;
    this->AvoidanceWeight = 0.00f;
    this->NavMeshProjectionInterval = 0.10f;
    this->NavMeshProjectionTimer = 0.00f;
    this->NavMeshProjectionInterpSpeed = 12.00f;
    this->NavMeshProjectionHeightScaleUp = 0.67f;
    this->NavMeshProjectionHeightScaleDown = 1.00f;
    this->NavWalkingFloorDistTolerance = 10.00f;
    this->MinTimeBetweenTimeStampResets = 240.00f;
    this->bWasSimulatingRootMotion = false;
    this->bAllowPhysicsRotationDuringAnimRootMotion = false;
}

void UCharacterMovementComponent::SetWalkableFloorZ(float InWalkableFloorZ) {
}

void UCharacterMovementComponent::SetWalkableFloorAngle(float InWalkableFloorAngle) {
}

void UCharacterMovementComponent::SetMovementMode(TEnumAsByte<EMovementMode> NewMovementMode, uint8 NewCustomMode) {
}

void UCharacterMovementComponent::SetGroupsToIgnoreMask(const FNavAvoidanceMask& GroupMask) {
}

void UCharacterMovementComponent::SetGroupsToIgnore(int32 GroupFlags) {
}

void UCharacterMovementComponent::SetGroupsToAvoidMask(const FNavAvoidanceMask& GroupMask) {
}

void UCharacterMovementComponent::SetGroupsToAvoid(int32 GroupFlags) {
}

void UCharacterMovementComponent::SetAvoidanceGroupMask(const FNavAvoidanceMask& GroupMask) {
}

void UCharacterMovementComponent::SetAvoidanceGroup(int32 GroupFlags) {
}

void UCharacterMovementComponent::SetAvoidanceEnabled(bool bEnable) {
}

void UCharacterMovementComponent::ServerMoveOld_Implementation(float OldTimeStamp, FVector_NetQuantize10 OldAccel, uint8 OldMoveFlags) {
}
bool UCharacterMovementComponent::ServerMoveOld_Validate(float OldTimeStamp, FVector_NetQuantize10 OldAccel, uint8 OldMoveFlags) {
    return true;
}

void UCharacterMovementComponent::ServerMoveDualHybridRootMotion_Implementation(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, float Timestamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, uint8 NewFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode) {
}
bool UCharacterMovementComponent::ServerMoveDualHybridRootMotion_Validate(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, float Timestamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, uint8 NewFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode) {
    return true;
}

void UCharacterMovementComponent::ServerMoveDual_Implementation(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, float Timestamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, uint8 NewFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode) {
}
bool UCharacterMovementComponent::ServerMoveDual_Validate(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, float Timestamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, uint8 NewFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode) {
    return true;
}

void UCharacterMovementComponent::ServerMove_Implementation(float Timestamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, uint8 CompressedMoveFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode) {
}
bool UCharacterMovementComponent::ServerMove_Validate(float Timestamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, uint8 CompressedMoveFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode) {
    return true;
}

float UCharacterMovementComponent::K2_GetWalkableFloorZ() const {
    return 0.0f;
}

float UCharacterMovementComponent::K2_GetWalkableFloorAngle() const {
    return 0.0f;
}

float UCharacterMovementComponent::K2_GetModifiedMaxAcceleration() const {
    return 0.0f;
}

void UCharacterMovementComponent::K2_FindFloor(FVector CapsuleLocation, FFindFloorResult& FloorResult) const {
}

void UCharacterMovementComponent::K2_ComputeFloorDist(FVector CapsuleLocation, float LineDistance, float SweepDistance, float SweepRadius, FFindFloorResult& FloorResult) const {
}

bool UCharacterMovementComponent::IsWalking() const {
    return false;
}

bool UCharacterMovementComponent::IsWalkable(const FHitResult& Hit) const {
    return false;
}

float UCharacterMovementComponent::GetValidPerchRadius() const {
    return 0.0f;
}

float UCharacterMovementComponent::GetPerchRadiusThreshold() const {
    return 0.0f;
}

UPrimitiveComponent* UCharacterMovementComponent::GetMovementBase() const {
    return NULL;
}

float UCharacterMovementComponent::GetMinAnalogSpeed() const {
    return 0.0f;
}

float UCharacterMovementComponent::GetMaxJumpHeightWithJumpTime() const {
    return 0.0f;
}

float UCharacterMovementComponent::GetMaxJumpHeight() const {
    return 0.0f;
}

float UCharacterMovementComponent::GetMaxBrakingDeceleration() const {
    return 0.0f;
}

float UCharacterMovementComponent::GetMaxAcceleration() const {
    return 0.0f;
}

FVector UCharacterMovementComponent::GetImpartedMovementBaseVelocity() const {
    return FVector{};
}

FVector UCharacterMovementComponent::GetCurrentAcceleration() const {
    return FVector{};
}

ACharacter* UCharacterMovementComponent::GetCharacterOwner() const {
    return NULL;
}

float UCharacterMovementComponent::GetAnalogInputModifier() const {
    return 0.0f;
}

void UCharacterMovementComponent::DisableMovement() {
}

void UCharacterMovementComponent::ClientVeryShortAdjustPosition_Implementation(float Timestamp, FVector NewLoc, UPrimitiveComponent* NewBase, FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode) {
}

void UCharacterMovementComponent::ClientAdjustRootMotionSourcePosition_Implementation(float Timestamp, FRootMotionSourceGroup ServerRootMotion, bool bHasAnimRootMotion, float ServerMontageTrackPosition, FVector ServerLoc, FVector_NetQuantizeNormal ServerRotation, float ServerVelZ, UPrimitiveComponent* ServerBase, FName ServerBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode) {
}

void UCharacterMovementComponent::ClientAdjustRootMotionPosition_Implementation(float Timestamp, float ServerMontageTrackPosition, FVector ServerLoc, FVector_NetQuantizeNormal ServerRotation, float ServerVelZ, UPrimitiveComponent* ServerBase, FName ServerBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode) {
}

void UCharacterMovementComponent::ClientAdjustPosition_Implementation(float Timestamp, FVector NewLoc, FVector NewVel, UPrimitiveComponent* NewBase, FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode) {
}

void UCharacterMovementComponent::ClientAckGoodMove_Implementation(float Timestamp) {
}

void UCharacterMovementComponent::ClearAccumulatedForces() {
}

void UCharacterMovementComponent::CapsuleTouched(UPrimitiveComponent* OverlappedComp, AActor* Other, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult) {
}

void UCharacterMovementComponent::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) {
}

void UCharacterMovementComponent::AddImpulse(FVector Impulse, bool bVelocityChange) {
}

void UCharacterMovementComponent::AddForce(FVector Force) {
}


