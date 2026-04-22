#include "Character.h"
#include "CapsuleComponent.h"
#include "CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "SkeletalMeshComponent.h"

ACharacter::ACharacter(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->RootComponent = CreateDefaultSubobject<UCapsuleComponent>(TEXT("CollisionCylinder"));
    this->bUseControllerRotationYaw = true;
    this->Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CharacterMesh0"));
    this->CharacterMovement = CreateDefaultSubobject<UCharacterMovementComponent>(TEXT("CharMoveComp"));
    this->CapsuleComponent = (UCapsuleComponent*)RootComponent;
    this->AnimRootMotionTranslationScale = 1.00f;
    this->ReplicatedServerLastTransformUpdateTimeStamp = 0.00f;
    this->ReplicatedMovementMode = 0;
    this->bInBaseReplication = false;
    this->CrouchedEyeHeight = 32.00f;
    this->bIsCrouched = false;
    this->bPressedJump = false;
    this->bClientUpdating = false;
    this->bClientWasFalling = false;
    this->bClientResimulateRootMotion = false;
    this->bClientResimulateRootMotionSources = false;
    this->bSimGravityDisabled = false;
    this->bClientCheckEncroachmentOnNetUpdate = true;
    this->bServerMoveIgnoreRootMotion = false;
    this->JumpKeyHoldTime = 0.00f;
    this->JumpMaxHoldTime = 0.00f;
    this->JumpMaxCount = 1;
    this->JumpCurrentCount = 0;
    this->bWasJumping = false;
    this->Mesh->SetupAttachment(RootComponent);
}

void ACharacter::UnCrouch(bool bClientSimulation) {
}

void ACharacter::StopJumping() {
}

void ACharacter::StopAnimMontage(UAnimMontage* AnimMontage) {
}

void ACharacter::SetReplicateMovement(bool bInReplicateMovement) {
}

void ACharacter::RootMotionDebugClientPrintOnScreen_Implementation(const FString& inString) {
}

float ACharacter::PlayAnimMontage(UAnimMontage* AnimMontage, float InPlayRate, FName StartSectionName) {
    return 0.0f;
}

void ACharacter::OnWalkingOffLedge_Implementation(const FVector& PreviousFloorImpactNormal, const FVector& PreviousFloorContactNormal, const FVector& PreviousLocation, float TimeDelta) {
}

void ACharacter::OnRep_RootMotion() {
}

void ACharacter::OnRep_ReplicatedBasedMovement() {
}

void ACharacter::OnRep_IsCrouched() {
}



void ACharacter::OnJumped_Implementation() {
}

void ACharacter::LaunchCharacter(FVector LaunchVelocity, bool bXYOverride, bool bZOverride) {
}





void ACharacter::Jump() {
}

bool ACharacter::IsPlayingRootMotion() const {
    return false;
}

bool ACharacter::IsPlayingNetworkedRootMotionMontage() const {
    return false;
}

bool ACharacter::IsJumpProvidingForce() const {
    return false;
}

UAnimMontage* ACharacter::GetCurrentMontage() {
    return NULL;
}

FVector ACharacter::GetBaseTranslationOffset() const {
    return FVector{};
}

FRotator ACharacter::GetBaseRotationOffsetRotator() const {
    return FRotator{};
}

float ACharacter::GetAnimRootMotionTranslationScale() const {
    return 0.0f;
}

void ACharacter::Crouch(bool bClientSimulation) {
}

void ACharacter::ClientCheatWalk_Implementation() {
}

void ACharacter::ClientCheatGhost_Implementation() {
}

void ACharacter::ClientCheatFly_Implementation() {
}

bool ACharacter::CanJumpInternal_Implementation() const {
    return false;
}

bool ACharacter::CanJump() const {
    return false;
}

void ACharacter::CacheInitialMeshOffset(FVector MeshRelativeLocation, FRotator MeshRelativeRotation) {
}

void ACharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    
    DOREPLIFETIME(ACharacter, ReplicatedBasedMovement);
    DOREPLIFETIME(ACharacter, AnimRootMotionTranslationScale);
    DOREPLIFETIME(ACharacter, ReplicatedServerLastTransformUpdateTimeStamp);
    DOREPLIFETIME(ACharacter, ReplicatedMovementMode);
    DOREPLIFETIME(ACharacter, bIsCrouched);
    DOREPLIFETIME(ACharacter, JumpMaxHoldTime);
    DOREPLIFETIME(ACharacter, JumpMaxCount);
    DOREPLIFETIME(ACharacter, RepRootMotion);
}


