#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Rotator -FallbackName=Rotator
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Transform -FallbackName=Transform
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Vector -FallbackName=Vector
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=BlueprintFunctionLibrary -FallbackName=BlueprintFunctionLibrary
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=EAttachLocation -FallbackName=EAttachLocation
#include "LuxGroundDebrisSetting.h"
#include "LuxVFxFunctionLibrary.generated.h"

class APostProcessVolume;
class ULuxGroundDebrisComponent;
class ULuxParticleSystemComponent;
class UMaterialInterface;
class UObject;
class UParticleSystem;
class USceneComponent;

UCLASS(Blueprintable)
class LUXORGAME_API ULuxVFxFunctionLibrary : public UBlueprintFunctionLibrary {
    GENERATED_BODY()
public:
    ULuxVFxFunctionLibrary();

    UFUNCTION(BlueprintCallable)
    static ULuxParticleSystemComponent* SpawnLuxEmitterAttached(UParticleSystem* Template, USceneComponent* AttachToComponent, FName AttachPointName, FVector Location, FRotator Rotation, FVector Scale3D, TEnumAsByte<EAttachLocation::Type> LocationType, bool bAutoDestroy);
    
    UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContextObject"))
    static ULuxParticleSystemComponent* SpawnLuxEmitterAtLocation(UObject* WorldContextObject, UParticleSystem* Template, FVector Location, FRotator Rotation, FVector Scale3D, bool bAutoDestroy);
    
    UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContextObject"))
    static ULuxGroundDebrisComponent* SpawnGroundDebris(UObject* WorldContextObject, const FLuxGroundDebrisSetting& Setting, const FTransform& Transform, bool bAutoDestroy);
    
    UFUNCTION(BlueprintCallable)
    static void RemovePostProcessMaterial(APostProcessVolume* PostProcessVolume, int32 index);
    
    UFUNCTION(BlueprintCallable, BlueprintPure, meta=(WorldContext="WorldContextObject"))
    static APostProcessVolume* GetPostProcessVolume(UObject* WorldContextObject);
    
    UFUNCTION(BlueprintCallable)
    static void DeactivatePostProcess(APostProcessVolume* PostProcessVolume, int32 index);
    
    UFUNCTION(BlueprintCallable)
    static int32 AddPostProcessMaterial(APostProcessVolume* PostProcessVolume, UMaterialInterface* Material);
    
    UFUNCTION(BlueprintCallable)
    static void ActivatePostProcess(APostProcessVolume* PostProcessVolume, int32 index);
    
};

