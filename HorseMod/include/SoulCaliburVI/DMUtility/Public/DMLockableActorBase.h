#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Vector -FallbackName=Vector
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=Actor -FallbackName=Actor
#include "DMLockableActorInterface.h"
#include "DMLockableActorBase.generated.h"

class UArrowComponent;
class UDMLockableComponent;

UCLASS(Blueprintable, Config=Game)
class DMUTILITY_API ADMLockableActorBase : public AActor, public IDMLockableActorInterface {
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Replicated, Transient, meta=(AllowPrivateAccess=true))
    TArray<TWeakObjectPtr<AActor>> Lockers;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    UArrowComponent* CustomRoot;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    UDMLockableComponent* LockHandler;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    int32 NumLockers;
    
    ADMLockableActorBase(const FObjectInitializer& ObjectInitializer);

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    FVector WhereToLookAt_Implementation();
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void SetLock_Implementation(AActor* inLocker);
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void ReleaseLock_Implementation(AActor* inLocker);
    
    UFUNCTION(BlueprintCallable)
    void OnSetLock(AActor* inLocker);
    
    UFUNCTION(BlueprintCallable)
    void OnReleaseLock(AActor* inLocker);
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void OnLockStateChanged();
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    bool IsLocked_Implementation();
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    bool IsLockable_Implementation(int32 inTeamNumber);
    

    // Fix for true pure virtual functions not being implemented
};

