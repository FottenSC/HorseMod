#pragma once
#include "CoreMinimal.h"
#include "MovementComponent.h"
#include "MovementProperties.h"
#include "NavAgentProperties.h"
#include "NavMovementComponent.generated.h"

UCLASS(Abstract, Blueprintable, ClassGroup=Custom, Config=Engine, meta=(BlueprintSpawnableComponent))
class ENGINE_API UNavMovementComponent : public UMovementComponent {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FNavAgentProperties NavAgentProps;
    
protected:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float FixedPathBrakingDistance;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bUpdateNavAgentWithOwnersCollision: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, GlobalConfig, meta=(AllowPrivateAccess=true))
    uint32 bUseAccelerationForPaths: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bUseFixedBrakingDistanceForPaths: 1;
    
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FMovementProperties MovementState;
    
    UNavMovementComponent(const FObjectInitializer& ObjectInitializer);

    UFUNCTION(BlueprintCallable)
    void StopMovementKeepPathing();
    
    UFUNCTION(BlueprintCallable)
    void StopActiveMovement();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool IsSwimming() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool IsMovingOnGround() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool IsFlying() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool IsFalling() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool IsCrouching() const;
    
};

