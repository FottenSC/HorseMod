#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Quat -FallbackName=Quat
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Rotator -FallbackName=Rotator
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Vector -FallbackName=Vector
#include "BasedMovementInfo.h"
#include "CharacterMovementUpdatedSignatureDelegate.h"
#include "CharacterReachedApexSignatureDelegate.h"
#include "EMovementMode.h"
#include "HitResult.h"
#include "MovementModeChangedSignatureDelegate.h"
#include "Pawn.h"
#include "RepRootMotionMontage.h"
#include "RootMotionMovementParams.h"
#include "RootMotionSourceGroup.h"
#include "SimulatedRootMotionReplicatedMove.h"
#include "Character.generated.h"

class UAnimMontage;
class UCapsuleComponent;
class UCharacterMovementComponent;
class USkeletalMeshComponent;

UCLASS(Blueprintable)
class ENGINE_API ACharacter : public APawn {
    GENERATED_BODY()
public:
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    USkeletalMeshComponent* Mesh;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    UCharacterMovementComponent* CharacterMovement;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    UCapsuleComponent* CapsuleComponent;
    
protected:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FBasedMovementInfo BasedMovement;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, ReplicatedUsing=OnRep_ReplicatedBasedMovement, meta=(AllowPrivateAccess=true))
    FBasedMovementInfo ReplicatedBasedMovement;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Replicated, meta=(AllowPrivateAccess=true))
    float AnimRootMotionTranslationScale;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FVector BaseTranslationOffset;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FQuat BaseRotationOffset;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Replicated, meta=(AllowPrivateAccess=true))
    float ReplicatedServerLastTransformUpdateTimeStamp;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Replicated, meta=(AllowPrivateAccess=true))
    uint8 ReplicatedMovementMode;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bInBaseReplication;
    
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float CrouchedEyeHeight;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, ReplicatedUsing=OnRep_IsCrouched, meta=(AllowPrivateAccess=true))
    uint32 bIsCrouched: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bPressedJump: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    uint32 bClientUpdating: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    uint32 bClientWasFalling: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    uint32 bClientResimulateRootMotion: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    uint32 bClientResimulateRootMotionSources: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bSimGravityDisabled: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    uint32 bClientCheckEncroachmentOnNetUpdate: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    uint32 bServerMoveIgnoreRootMotion: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    float JumpKeyHoldTime;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Replicated, meta=(AllowPrivateAccess=true))
    float JumpMaxHoldTime;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Replicated, meta=(AllowPrivateAccess=true))
    int32 JumpMaxCount;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    int32 JumpCurrentCount;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    uint32 bWasJumping: 1;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FCharacterReachedApexSignature OnReachedJumpApex;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FMovementModeChangedSignature MovementModeChangedDelegate;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FCharacterMovementUpdatedSignature OnCharacterMovementUpdated;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    FRootMotionSourceGroup SavedRootMotion;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    FRootMotionMovementParams ClientRootMotionParams;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    TArray<FSimulatedRootMotionReplicatedMove> RootMotionRepMoves;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, ReplicatedUsing=OnRep_RootMotion, meta=(AllowPrivateAccess=true))
    FRepRootMotionMontage RepRootMotion;
    
    ACharacter(const FObjectInitializer& ObjectInitializer);

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UFUNCTION(BlueprintCallable)
    void UnCrouch(bool bClientSimulation);
    
    UFUNCTION(BlueprintCallable)
    void StopJumping();
    
    UFUNCTION(BlueprintCallable)
    void StopAnimMontage(UAnimMontage* AnimMontage);
    
    UFUNCTION(BlueprintCallable)
    void SetReplicateMovement(bool bInReplicateMovement);
    
    UFUNCTION(BlueprintCallable, Client, Reliable)
    void RootMotionDebugClientPrintOnScreen(const FString& inString);
    
    UFUNCTION(BlueprintCallable)
    float PlayAnimMontage(UAnimMontage* AnimMontage, float InPlayRate, FName StartSectionName);
    
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent)
    void OnWalkingOffLedge(const FVector& PreviousFloorImpactNormal, const FVector& PreviousFloorContactNormal, const FVector& PreviousLocation, float TimeDelta);
    
    UFUNCTION(BlueprintCallable)
    void OnRep_RootMotion();
    
    UFUNCTION(BlueprintCallable)
    void OnRep_ReplicatedBasedMovement();
    
    UFUNCTION(BlueprintCallable)
    void OnRep_IsCrouched();
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void OnLaunched(FVector LaunchVelocity, bool bXYOverride, bool bZOverride);
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void OnLanded(const FHitResult& Hit);
    
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent)
    void OnJumped();
    
    UFUNCTION(BlueprintCallable)
    void LaunchCharacter(FVector LaunchVelocity, bool bXYOverride, bool bZOverride);
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void K2_UpdateCustomMovement(float DeltaTime);
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void K2_OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust);
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void K2_OnMovementModeChanged(TEnumAsByte<EMovementMode> PrevMovementMode, TEnumAsByte<EMovementMode> NewMovementMode, uint8 PrevCustomMode, uint8 NewCustomMode);
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void K2_OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust);
    
    UFUNCTION(BlueprintCallable)
    void Jump();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool IsPlayingRootMotion() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool IsPlayingNetworkedRootMotionMontage() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool IsJumpProvidingForce() const;
    
    UFUNCTION(BlueprintCallable)
    UAnimMontage* GetCurrentMontage();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FVector GetBaseTranslationOffset() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FRotator GetBaseRotationOffsetRotator() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    float GetAnimRootMotionTranslationScale() const;
    
    UFUNCTION(BlueprintCallable)
    void Crouch(bool bClientSimulation);
    
    UFUNCTION(BlueprintCallable, Client, Reliable)
    void ClientCheatWalk();
    
    UFUNCTION(BlueprintCallable, Client, Reliable)
    void ClientCheatGhost();
    
    UFUNCTION(BlueprintCallable, Client, Reliable)
    void ClientCheatFly();
    
protected:
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent)
    bool CanJumpInternal() const;
    
public:
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool CanJump() const;
    
    UFUNCTION(BlueprintCallable)
    void CacheInitialMeshOffset(FVector MeshRelativeLocation, FRotator MeshRelativeRotation);
    
};

