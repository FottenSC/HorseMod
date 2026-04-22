#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=LinearColor -FallbackName=LinearColor
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Rotator -FallbackName=Rotator
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Transform -FallbackName=Transform
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Vector -FallbackName=Vector
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=EAttachLocation -FallbackName=EAttachLocation
#include "LuxActor.h"
#include "LuxGroundDebrisSetting.h"
#include "OnVFxFinishedDelegate.h"
#include "VFxDestroyParam.h"
#include "VFxEventParam.h"
#include "VFxHiddenParam.h"
#include "VFxSettingListDataKey.h"
#include "VFxSettingListDataValue.h"
#include "VFxSpawnParam.h"
#include "LuxVFxInstanceManager.generated.h"

class ULuxGroundDebrisComponent;
class ULuxParticleSystemComponent;
class UParticleSystem;
class UParticleSystemComponent;
class USceneComponent;

UCLASS(Blueprintable)
class LUXORGAME_API ALuxVFxInstanceManager : public ALuxActor {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FOnVFxFinished OnVFxFinished;
    
    ALuxVFxInstanceManager(const FObjectInitializer& ObjectInitializer);

    UFUNCTION(BlueprintCallable)
    int32 SpawnParticleAttached(UParticleSystem* Template, USceneComponent* AttachToComponent, FName AttachPointName, FVector Location, FRotator Rotation, FVector Scale3D, TEnumAsByte<EAttachLocation::Type> LocationType);
    
    UFUNCTION(BlueprintCallable)
    int32 SpawnParticleAtLocation(UParticleSystem* Template, FVector Location, FRotator Rotation, FVector Scale3D);
    
    UFUNCTION(BlueprintCallable)
    int32 SpawnGroundDebris(const FLuxGroundDebrisSetting& Setting, const FTransform& Transform);
    
    UFUNCTION(BlueprintCallable)
    void SetVectorParam(int32 InstanceID, const FName& ParamName, const FVector& VectorParam);
    
    UFUNCTION(BlueprintCallable)
    void SetScalarParam(int32 InstanceID, const FName& ParamName, float ScalarParam);
    
    UFUNCTION(BlueprintCallable)
    void SetHiddenSetting(const FVFxHiddenParam& HiddenParam, bool Hidden);
    
    UFUNCTION(BlueprintCallable)
    void SetGroupTimeDilation(int32 Group, float TimeDilation);
    
    UFUNCTION(BlueprintCallable)
    void SetColorParam(int32 InstanceID, const FName& ParamName, const FLinearColor& ColorParam);
    
    UFUNCTION(BlueprintCallable)
    int32 RequestSpawn(const FVFxSpawnParam& SpawnParam);
    
    UFUNCTION(BlueprintCallable)
    void RequestGenerateEvent(const FVFxEventParam& EventParam);
    
    UFUNCTION(BlueprintCallable)
    void RequestDestroy(const FVFxDestroyParam& DestroyParam);
    
private:
    UFUNCTION(BlueprintCallable)
    void OnParticleSystemFinished(UParticleSystemComponent* FinishedPSComponent);
    
    UFUNCTION(BlueprintCallable)
    void OnGroundDebrisDeactivated(ULuxGroundDebrisComponent* DeactivatedGDComponent);
    
public:
    UFUNCTION(BlueprintCallable)
    ULuxParticleSystemComponent* GetParticle(int32 InstanceID);
    
    UFUNCTION(BlueprintCallable)
    ULuxGroundDebrisComponent* GetGroundDebris(int32 InstanceID);
    
    UFUNCTION(BlueprintCallable)
    void DestroyParticle(int32 InstanceID, bool bImmediately);
    
    UFUNCTION(BlueprintCallable)
    void DestroyGroundDebris(int32 InstanceID, bool bImmediately);
    
    UFUNCTION(BlueprintCallable)
    void DestroyAllVFx(bool bImmediately);
    
    UFUNCTION(BlueprintCallable)
    void DestroyAllParticle(bool bImmediately);
    
    UFUNCTION(BlueprintCallable)
    void DestroyAllGroundDebris(bool bImmediately);
    
    UFUNCTION(BlueprintCallable)
    void ClearSettingList();
    
    UFUNCTION(BlueprintCallable)
    void ClearHiddenSetting();
    
    UFUNCTION(BlueprintCallable)
    void AddVFxSettingListData(const FVFxSettingListDataKey& Key, const FVFxSettingListDataValue& Value);
    
};

