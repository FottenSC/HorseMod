#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Vector -FallbackName=Vector
#include "ELuxTracePartsId.h"
#include "LuxActor.h"
#include "TraceActiveParam.h"
#include "TraceInactiveParam.h"
#include "LuxTraceManager.generated.h"

class AActor;
class ULuxCreationProfile;
class ULuxTraceComponent;
class ULuxTraceDataAsset;
class USceneComponent;

UCLASS(Blueprintable)
class LUXORGAME_API ALuxTraceManager : public ALuxActor {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    ULuxTraceDataAsset* TraceDataAsset;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    ULuxCreationProfile* CreationProfile;
    
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    USceneComponent* CharaAttachComponent;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    USceneComponent* WeaponAttachComponent;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    ULuxTraceComponent* TraceComponent;
    
public:
    ALuxTraceManager(const FObjectInitializer& ObjectInitializer);

    UFUNCTION(BlueprintCallable)
    void Stop();
    
    UFUNCTION(BlueprintCallable)
    void SetVisibility(int32 InAttachIndex, bool bVisibility);
    
    UFUNCTION(BlueprintCallable)
    void Setup(int32 InAttachIndex, AActor* InAttachActor, USceneComponent* InAttachComponent, int32 PlayerIndex);
    
    UFUNCTION(BlueprintCallable)
    void SetElapsedTime(float InElapsedTime, float InElapsedTimeWithStop);
    
    UFUNCTION(BlueprintCallable)
    void Inactive(const FTraceInactiveParam& Param);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool GetTracePosition(ELuxTracePartsId InTracePartsId, int32 inPlayerIndex, FVector& TipPosition, FVector& HiltPosition) const;
    
    UFUNCTION(BlueprintCallable)
    void DestroyTraceComponent();
    
    UFUNCTION(BlueprintCallable)
    void Active(const FTraceActiveParam& Param);
    
};

