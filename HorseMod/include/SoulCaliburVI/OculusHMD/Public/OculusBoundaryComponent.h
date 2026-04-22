#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Color -FallbackName=Color
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Vector -FallbackName=Vector
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=ActorComponent -FallbackName=ActorComponent
#include "BoundaryTestResult.h"
#include "ETrackedDeviceType.h"
#include "OculusBoundaryComponent.generated.h"

UCLASS(Blueprintable, MinimalAPI, ClassGroup=Custom, meta=(BlueprintSpawnableComponent))
class UOculusBoundaryComponent : public UActorComponent {
    GENERATED_BODY()
public:
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOculusOuterBoundaryTriggeredEvent, const TArray<FBoundaryTestResult>&, OuterBoundsInteractionList);
    DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOculusOuterBoundaryReturnedEvent);
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FOculusOuterBoundaryTriggeredEvent OnOuterBoundaryTriggered;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FOculusOuterBoundaryReturnedEvent OnOuterBoundaryReturned;
    
    UOculusBoundaryComponent(const FObjectInitializer& ObjectInitializer);

    UFUNCTION(BlueprintCallable)
    bool SetOuterBoundaryColor(const FColor InBoundaryColor);
    
    UFUNCTION(BlueprintCallable)
    bool ResetOuterBoundaryColor();
    
    UFUNCTION(BlueprintCallable)
    bool RequestOuterBoundaryVisible(bool BoundaryVisible);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool IsOuterBoundaryTriggered();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool IsOuterBoundaryDisplayed();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FBoundaryTestResult GetTriggeredPlayAreaInfo(ETrackedDeviceType DeviceType);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    TArray<FBoundaryTestResult> GetTriggeredOuterBoundaryInfo();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    TArray<FVector> GetPlayAreaPoints();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FVector GetPlayAreaDimensions();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    TArray<FVector> GetOuterBoundaryPoints();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FVector GetOuterBoundaryDimensions();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FBoundaryTestResult CheckIfPointWithinPlayArea(const FVector Point);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FBoundaryTestResult CheckIfPointWithinOuterBounds(const FVector Point);
    
};

