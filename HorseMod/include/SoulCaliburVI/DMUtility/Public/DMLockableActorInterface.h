#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Interface -FallbackName=Interface
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Vector -FallbackName=Vector
#include "DMLockableActorInterface.generated.h"

class AActor;

UINTERFACE(Blueprintable)
class DMUTILITY_API UDMLockableActorInterface : public UInterface {
    GENERATED_BODY()
};

class DMUTILITY_API IDMLockableActorInterface : public IInterface {
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent)
    FVector WhereToLookAt();
    
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent)
    void SetLock(AActor* inLocker);
    
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent)
    void ReleaseLock(AActor* inLocker);
    
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent)
    bool IsLocked();
    
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent)
    bool IsLockable(int32 inTeamNumber);
    
};

