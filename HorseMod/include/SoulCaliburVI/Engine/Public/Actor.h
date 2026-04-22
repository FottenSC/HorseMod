#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Rotator -FallbackName=Rotator
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Transform -FallbackName=Transform
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Vector -FallbackName=Vector
//CROSS-MODULE INCLUDE V2: -ModuleName=InputCore -ObjectName=ETouchIndex -FallbackName=ETouchIndex
//CROSS-MODULE INCLUDE V2: -ModuleName=InputCore -ObjectName=Key -FallbackName=Key
#include "ActorBeginCursorOverSignatureDelegate.h"
#include "ActorBeginOverlapSignatureDelegate.h"
#include "ActorBeginTouchOverSignatureDelegate.h"
#include "ActorDestroyedSignatureDelegate.h"
#include "ActorEndCursorOverSignatureDelegate.h"
#include "ActorEndOverlapSignatureDelegate.h"
#include "ActorEndPlaySignatureDelegate.h"
#include "ActorEndTouchOverSignatureDelegate.h"
#include "ActorHitSignatureDelegate.h"
#include "ActorOnClickedSignatureDelegate.h"
#include "ActorOnInputTouchBeginSignatureDelegate.h"
#include "ActorOnInputTouchEndSignatureDelegate.h"
#include "ActorOnReleasedSignatureDelegate.h"
#include "ActorTickFunction.h"
#include "EAttachLocation.h"
#include "EAttachmentRule.h"
#include "EAutoReceiveInput.h"
#include "EDetachmentRule.h"
#include "EEndPlayReason.h"
#include "EInputConsumeOptions.h"
#include "ENetRole.h"
#include "ESpawnActorCollisionHandlingMethod.h"
#include "ETickingGroup.h"
#include "HitResult.h"
#include "RepAttachment.h"
#include "RepMovement.h"
#include "TakeAnyDamageSignatureDelegate.h"
#include "TakePointDamageSignatureDelegate.h"
#include "Templates/SubclassOf.h"
#include "Actor.generated.h"

class AActor;
class AController;
class AMatineeActor;
class APawn;
class APlayerController;
class UActorComponent;
class UChildActorComponent;
class UDamageType;
class UInputComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPrimitiveComponent;
class USceneComponent;

UCLASS(Blueprintable)
class ENGINE_API AActor : public UObject {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FActorTickFunction PrimaryActorTick;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float CustomTimeDilation;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Interp, Replicated, meta=(AllowPrivateAccess=true))
    uint32 bHidden: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bNetTemporary: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bNetStartup: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bOnlyRelevantToOwner: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bAlwaysRelevant: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, ReplicatedUsing=OnRep_ReplicateMovement, meta=(AllowPrivateAccess=true))
    uint32 bReplicateMovement: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Replicated, meta=(AllowPrivateAccess=true))
    uint32 bTearOff: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    uint32 bExchangedRoles: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bNetLoadOnClient: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bNetUseOwnerRelevancy: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bBlockInput: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bAllowTickBeforeBeginPlay: 1;
    
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bActorEnableCollision: 1;
    
protected:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bReplicates: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FName NetDriverName;
    
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Replicated, Transient, meta=(AllowPrivateAccess=true))
    TEnumAsByte<ENetRole> RemoteRole;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, ReplicatedUsing=OnRep_Owner, meta=(AllowPrivateAccess=true))
    AActor* Owner;
    
public:
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, ReplicatedUsing=OnRep_ReplicatedMovement, meta=(AllowPrivateAccess=true))
    FRepMovement ReplicatedMovement;
    
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, ReplicatedUsing=OnRep_AttachmentReplication, meta=(AllowPrivateAccess=true))
    FRepAttachment AttachmentReplication;
    
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Replicated, meta=(AllowPrivateAccess=true))
    TEnumAsByte<ENetRole> Role;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TEnumAsByte<EAutoReceiveInput::Type> AutoReceiveInput;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    int32 InputPriority;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    UInputComponent* InputComponent;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TEnumAsByte<EInputConsumeOptions> InputConsumeOption;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float NetCullDistanceSquared;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    int32 NetTag;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float NetUpdateFrequency;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float MinNetUpdateFrequency;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float NetPriority;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bAutoDestroyWhenFinished: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Replicated, SaveGame, meta=(AllowPrivateAccess=true))
    uint32 bCanBeDamaged: 1;
    
private:
    UPROPERTY(BlueprintReadWrite, DuplicateTransient, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    uint32 bActorIsBeingDestroyed: 1;
    
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bCollideWhenPlacing: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bFindCameraComponentWhenViewTarget: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bRelevantForNetworkReplays: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bGenerateOverlapEventsDuringLevelStreaming: 1;
    
protected:
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bCanBeInCluster: 1;
    
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    ESpawnActorCollisionHandlingMethod SpawnCollisionHandlingMethod;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, ReplicatedUsing=OnRep_Instigator, meta=(AllowPrivateAccess=true))
    APawn* Instigator;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    TArray<AActor*> Children;
    
protected:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    USceneComponent* RootComponent;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    TArray<AMatineeActor*> ControllingMatineeActors;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float InitialLifeSpan;
    
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FName> Layers;
    
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TWeakObjectPtr<AActor> ParentComponentActor;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Export, meta=(AllowPrivateAccess=true))
    TWeakObjectPtr<UChildActorComponent> ParentComponent;
    
protected:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bAllowReceiveTickEventOnDedicatedServer: 1;
    
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bActorSeamlessTraveled: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bIgnoresOriginShifting: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bEnableAutoLODGeneration: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FName> Tags;
    
    UPROPERTY(EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    uint64 HiddenEditorViews;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FTakeAnyDamageSignature OnTakeAnyDamage;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FTakePointDamageSignature OnTakePointDamage;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FActorBeginOverlapSignature OnActorBeginOverlap;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FActorEndOverlapSignature OnActorEndOverlap;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FActorBeginCursorOverSignature OnBeginCursorOver;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FActorEndCursorOverSignature OnEndCursorOver;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FActorOnClickedSignature OnClicked;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FActorOnReleasedSignature OnReleased;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FActorOnInputTouchBeginSignature OnInputTouchBegin;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FActorOnInputTouchEndSignature OnInputTouchEnd;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FActorBeginTouchOverSignature OnInputTouchEnter;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FActorEndTouchOverSignature OnInputTouchLeave;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FActorHitSignature OnActorHit;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FActorDestroyedSignature OnDestroyed;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FActorEndPlaySignature OnEndPlay;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, NonTransactional, TextExportTransient, meta=(AllowPrivateAccess=true))
    TArray<UActorComponent*> BlueprintCreatedComponents;
    
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    TArray<UActorComponent*> InstanceComponents;
    
public:
    AActor(const FObjectInitializer& ObjectInitializer);

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool WasRecentlyRendered(float Tolerance) const;
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void UserConstructionScript();
    
    UFUNCTION(BlueprintCallable)
    void TearOff();
    
    UFUNCTION(BlueprintCallable)
    void SnapRootComponentTo(AActor* InParentActor, FName InSocketName);
    
    UFUNCTION(BlueprintCallable)
    void SetTickGroup(TEnumAsByte<ETickingGroup> NewTickGroup);
    
    UFUNCTION(BlueprintCallable)
    void SetTickableWhenPaused(bool bTickableWhenPaused);
    
    UFUNCTION(BlueprintAuthorityOnly, BlueprintCallable)
    void SetReplicates(bool bInReplicates);
    
    UFUNCTION(BlueprintCallable)
    void SetReplicateMovement(bool bInReplicateMovement);
    
    UFUNCTION(BlueprintCallable)
    void SetOwner(AActor* NewOwner);
    
    UFUNCTION(BlueprintCallable)
    void SetLifeSpan(float InLifespan);
    
    UFUNCTION(BlueprintCallable)
    void SetActorTickInterval(float TickInterval);
    
    UFUNCTION(BlueprintCallable)
    void SetActorTickEnabled(bool bEnabled);
    
    UFUNCTION(BlueprintCallable)
    void SetActorScale3D(FVector NewScale3D);
    
    UFUNCTION(BlueprintCallable)
    void SetActorRelativeScale3D(FVector NewRelativeScale);
    
    UFUNCTION(BlueprintCallable)
    void SetActorHiddenInGame(bool bNewHidden);
    
    UFUNCTION(BlueprintCallable)
    void SetActorEnableCollision(bool bNewActorEnableCollision);
    
    UFUNCTION(BlueprintCallable)
    void RemoveTickPrerequisiteComponent(UActorComponent* PrerequisiteComponent);
    
    UFUNCTION(BlueprintCallable)
    void RemoveTickPrerequisiteActor(AActor* PrerequisiteActor);
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void ReceiveTick(float DeltaSeconds);
    
    UFUNCTION(BlueprintAuthorityOnly, BlueprintCallable, BlueprintImplementableEvent)
    void ReceiveRadialDamage(float DamageReceived, const UDamageType* DamageType, FVector Origin, const FHitResult& HitInfo, AController* InstigatedBy, AActor* DamageCauser);
    
    UFUNCTION(BlueprintAuthorityOnly, BlueprintCallable, BlueprintImplementableEvent)
    void ReceivePointDamage(float Damage, const UDamageType* DamageType, FVector HitLocation, FVector HitNormal, UPrimitiveComponent* HitComponent, FName BoneName, FVector ShotFromDirection, AController* InstigatedBy, AActor* DamageCauser, const FHitResult& HitInfo);
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void ReceiveHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit);
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void ReceiveEndPlay(TEnumAsByte<EEndPlayReason::Type> EndPlayReason);
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void ReceiveDestroyed();
    
protected:
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void ReceiveBeginPlay();
    
public:
    UFUNCTION(BlueprintAuthorityOnly, BlueprintCallable, BlueprintImplementableEvent)
    void ReceiveAnyDamage(float Damage, const UDamageType* DamageType, AController* InstigatedBy, AActor* DamageCauser);
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void ReceiveActorOnReleased(FKey ButtonReleased);
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void ReceiveActorOnInputTouchLeave(const TEnumAsByte<ETouchIndex::Type> FingerIndex);
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void ReceiveActorOnInputTouchEnter(const TEnumAsByte<ETouchIndex::Type> FingerIndex);
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void ReceiveActorOnInputTouchEnd(const TEnumAsByte<ETouchIndex::Type> FingerIndex);
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void ReceiveActorOnInputTouchBegin(const TEnumAsByte<ETouchIndex::Type> FingerIndex);
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void ReceiveActorOnClicked(FKey ButtonPressed);
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void ReceiveActorEndOverlap(AActor* OtherActor);
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void ReceiveActorEndCursorOver();
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void ReceiveActorBeginOverlap(AActor* OtherActor);
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void ReceiveActorBeginCursorOver();
    
    UFUNCTION(BlueprintCallable)
    void OnRep_ReplicateMovement();
    
    UFUNCTION(BlueprintCallable)
    void OnRep_ReplicatedMovement();
    
protected:
    UFUNCTION(BlueprintCallable)
    void OnRep_Owner();
    
public:
    UFUNCTION(BlueprintCallable)
    void OnRep_Instigator();
    
    UFUNCTION(BlueprintCallable)
    void OnRep_AttachmentReplication();
    
    UFUNCTION(BlueprintAuthorityOnly, BlueprintCallable)
    void MakeNoise(float Loudness, APawn* NoiseInstigator, FVector NoiseLocation, float MaxRange, FName Tag);
    
    UFUNCTION(BlueprintCallable)
    UMaterialInstanceDynamic* MakeMIDForMaterial(UMaterialInterface* Parent);
    
    UFUNCTION(BlueprintCallable)
    bool K2_TeleportTo(FVector DestLocation, FRotator DestRotation);
    
    UFUNCTION(BlueprintCallable)
    bool K2_SetActorTransform(const FTransform& NewTransform, bool bSweep, FHitResult& SweepHitResult, bool bTeleport);
    
    UFUNCTION(BlueprintCallable)
    bool K2_SetActorRotation(FRotator NewRotation, bool bTeleportPhysics);
    
    UFUNCTION(BlueprintCallable)
    void K2_SetActorRelativeTransform(const FTransform& NewRelativeTransform, bool bSweep, FHitResult& SweepHitResult, bool bTeleport);
    
    UFUNCTION(BlueprintCallable)
    void K2_SetActorRelativeRotation(FRotator NewRelativeRotation, bool bSweep, FHitResult& SweepHitResult, bool bTeleport);
    
    UFUNCTION(BlueprintCallable)
    void K2_SetActorRelativeLocation(FVector NewRelativeLocation, bool bSweep, FHitResult& SweepHitResult, bool bTeleport);
    
    UFUNCTION(BlueprintCallable)
    bool K2_SetActorLocationAndRotation(FVector NewLocation, FRotator NewRotation, bool bSweep, FHitResult& SweepHitResult, bool bTeleport);
    
    UFUNCTION(BlueprintCallable)
    bool K2_SetActorLocation(FVector NewLocation, bool bSweep, FHitResult& SweepHitResult, bool bTeleport);
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void K2_OnReset();
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void K2_OnEndViewTarget(APlayerController* PC);
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void K2_OnBecomeViewTarget(APlayerController* PC);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    USceneComponent* K2_GetRootComponent() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FRotator K2_GetActorRotation() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FVector K2_GetActorLocation() const;
    
    UFUNCTION(BlueprintCallable)
    void K2_DetachFromActor(EDetachmentRule LocationRule, EDetachmentRule RotationRule, EDetachmentRule ScaleRule);
    
    UFUNCTION(BlueprintCallable)
    void K2_DestroyComponent(UActorComponent* Component);
    
    UFUNCTION(BlueprintCallable)
    void K2_DestroyActor();
    
    UFUNCTION(BlueprintCallable)
    void K2_AttachToComponent(USceneComponent* Parent, FName SocketName, EAttachmentRule LocationRule, EAttachmentRule RotationRule, EAttachmentRule ScaleRule, bool bWeldSimulatedBodies);
    
    UFUNCTION(BlueprintCallable)
    void K2_AttachToActor(AActor* ParentActor, FName SocketName, EAttachmentRule LocationRule, EAttachmentRule RotationRule, EAttachmentRule ScaleRule, bool bWeldSimulatedBodies);
    
    UFUNCTION(BlueprintCallable)
    void K2_AttachRootComponentToActor(AActor* InParentActor, FName InSocketName, TEnumAsByte<EAttachLocation::Type> AttachLocationType, bool bWeldSimulatedBodies);
    
    UFUNCTION(BlueprintCallable)
    void K2_AttachRootComponentTo(USceneComponent* InParent, FName InSocketName, TEnumAsByte<EAttachLocation::Type> AttachLocationType, bool bWeldSimulatedBodies);
    
    UFUNCTION(BlueprintCallable)
    void K2_AddActorWorldTransform(const FTransform& DeltaTransform, bool bSweep, FHitResult& SweepHitResult, bool bTeleport);
    
    UFUNCTION(BlueprintCallable)
    void K2_AddActorWorldRotation(FRotator DeltaRotation, bool bSweep, FHitResult& SweepHitResult, bool bTeleport);
    
    UFUNCTION(BlueprintCallable)
    void K2_AddActorWorldOffset(FVector DeltaLocation, bool bSweep, FHitResult& SweepHitResult, bool bTeleport);
    
    UFUNCTION(BlueprintCallable)
    void K2_AddActorLocalTransform(const FTransform& NewTransform, bool bSweep, FHitResult& SweepHitResult, bool bTeleport);
    
    UFUNCTION(BlueprintCallable)
    void K2_AddActorLocalRotation(FRotator DeltaRotation, bool bSweep, FHitResult& SweepHitResult, bool bTeleport);
    
    UFUNCTION(BlueprintCallable)
    void K2_AddActorLocalOffset(FVector DeltaLocation, bool bSweep, FHitResult& SweepHitResult, bool bTeleport);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool IsOverlappingActor(const AActor* Other) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool IsChildActor() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool IsActorTickEnabled() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool IsActorBeingDestroyed() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool HasAuthority() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    float GetVerticalDistanceTo(const AActor* OtherActor) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FVector GetVelocity() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FTransform GetTransform() const;
    
    UFUNCTION(BlueprintCallable)
    bool GetTickableWhenPaused();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    float GetSquaredDistanceTo(const AActor* OtherActor) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    TEnumAsByte<ENetRole> GetRemoteRole() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    UChildActorComponent* GetParentComponent() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    AActor* GetParentActor() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    AActor* GetOwner() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    void GetOverlappingComponents(TArray<UPrimitiveComponent*>& OverlappingComponents) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    void GetOverlappingActors(TArray<AActor*>& OverlappingActors, TSubclassOf<AActor> ClassFilter) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    float GetLifeSpan() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    AController* GetInstigatorController() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    APawn* GetInstigator() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FVector GetInputVectorAxisValue(const FKey InputAxisKey) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    float GetInputAxisValue(const FName InputAxisName) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    float GetInputAxisKeyValue(const FKey InputAxisKey) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    float GetHorizontalDotProductTo(const AActor* OtherActor) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    float GetHorizontalDistanceTo(const AActor* OtherActor) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    float GetGameTimeSinceCreation();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    float GetDotProductTo(const AActor* OtherActor) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    float GetDistanceTo(const AActor* OtherActor) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    TArray<UActorComponent*> GetComponentsByTag(TSubclassOf<UActorComponent> ComponentClass, FName Tag) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    TArray<UActorComponent*> GetComponentsByClass(TSubclassOf<UActorComponent> ComponentClass) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    UActorComponent* GetComponentByClass(TSubclassOf<UActorComponent> ComponentClass) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FName GetAttachParentSocketName() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    AActor* GetAttachParentActor() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    void GetAttachedActors(TArray<AActor*>& OutActors) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    void GetAllChildActors(TArray<AActor*>& ChildActors, bool bIncludeDescendants) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FVector GetActorUpVector() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    float GetActorTimeDilation() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    float GetActorTickInterval() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FVector GetActorScale3D() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FVector GetActorRightVector() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FVector GetActorRelativeScale3D() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FVector GetActorForwardVector() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    void GetActorEyesViewPoint(FVector& OutLocation, FRotator& OutRotation) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool GetActorEnableCollision() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    void GetActorBounds(bool bOnlyCollidingComponents, FVector& Origin, FVector& BoxExtent) const;
    
    UFUNCTION(BlueprintAuthorityOnly, BlueprintCallable)
    void ForceNetUpdate();
    
    UFUNCTION(BlueprintAuthorityOnly, BlueprintCallable)
    void FlushNetDormancy();
    
    UFUNCTION(BlueprintCallable)
    void EnableInput(APlayerController* PlayerController);
    
    UFUNCTION(BlueprintCallable)
    void DisableInput(APlayerController* PlayerController);
    
    UFUNCTION(BlueprintCallable)
    void DetachRootComponentFromParent(bool bMaintainWorldPosition);
    
    UFUNCTION(BlueprintCallable)
    void AddTickPrerequisiteComponent(UActorComponent* PrerequisiteComponent);
    
    UFUNCTION(BlueprintCallable)
    void AddTickPrerequisiteActor(AActor* PrerequisiteActor);
    
    UFUNCTION(BlueprintCallable)
    UActorComponent* AddComponent(FName TemplateName, bool bManualAttachment, const FTransform& RelativeTransform, const UObject* ComponentTemplateContext);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool ActorHasTag(FName Tag) const;
    
};

