#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Rotator -FallbackName=Rotator
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Vector -FallbackName=Vector
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Vector2D -FallbackName=Vector2D
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=BlueprintFunctionLibrary -FallbackName=BlueprintFunctionLibrary
//CROSS-MODULE INCLUDE V2: -ModuleName=HeadMountedDisplay -ObjectName=EOrientPositionSelector -FallbackName=EOrientPositionSelector
#include "EGearVRControllerHandedness_DEPRECATED.h"
#include "ETrackedDeviceType.h"
#include "HmdUserProfile.h"
#include "OculusFunctionLibrary.generated.h"

class UTexture2D;

UCLASS(Blueprintable)
class OCULUSHMD_API UOculusFunctionLibrary : public UBlueprintFunctionLibrary {
    GENERATED_BODY()
public:
    UOculusFunctionLibrary();

    UFUNCTION(BlueprintCallable)
    static void ShowLoadingSplashScreen();
    
    UFUNCTION(BlueprintCallable)
    static void ShowLoadingIcon(UTexture2D* Texture);
    
    UFUNCTION(BlueprintCallable)
    static void SetPositionScale3D(FVector PosScale3D);
    
    UFUNCTION(BlueprintCallable)
    static void SetLoadingSplashParams(const FString& TexturePath, FVector DistanceInMeters, FVector2D SizeInMeters, FVector RotationAxis, float RotationDeltaInDeg);
    
    UFUNCTION(BlueprintCallable)
    static void SetCPUAndGPULevels(int32 CpuLevel, int32 GPULevel);
    
    UFUNCTION(BlueprintCallable)
    static void SetBaseRotationAndPositionOffset(FRotator BaseRot, FVector PosOffset, TEnumAsByte<EOrientPositionSelector::Type> Options);
    
    UFUNCTION(BlueprintCallable)
    static void SetBaseRotationAndBaseOffsetInMeters(FRotator Rotation, FVector BaseOffsetInMeters, TEnumAsByte<EOrientPositionSelector::Type> Options);
    
private:
    UFUNCTION(BlueprintCallable)
    static bool IsPowerLevelStateThrottled();
    
    UFUNCTION(BlueprintCallable)
    static bool IsPowerLevelStateMinimum();
    
public:
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool IsLoadingIconEnabled();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool IsDeviceTracked(ETrackedDeviceType DeviceType);
    
private:
    UFUNCTION(BlueprintCallable)
    static bool IsControllerActive();
    
public:
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool IsAutoLoadingSplashScreenEnabled();
    
    UFUNCTION(BlueprintCallable)
    static void HideLoadingSplashScreen(bool bClear);
    
    UFUNCTION(BlueprintCallable)
    static void HideLoadingIcon();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool GetUserProfile(FHmdUserProfile& Profile);
    
private:
    UFUNCTION(BlueprintCallable)
    static float GetTemperatureInCelsius();
    
public:
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static void GetRawSensorData(FVector& AngularAcceleration, FVector& LinearAcceleration, FVector& AngularVelocity, FVector& LinearVelocity, float& TimeInSeconds, ETrackedDeviceType DeviceType);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static void GetPose(FRotator& DeviceRotation, FVector& DevicePosition, FVector& NeckPosition, bool bUseOrienationForPlayerCamera, bool bUsePositionForPlayerCamera, const FVector PositionScale);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static void GetLoadingSplashParams(FString& TexturePath, FVector& DistanceInMeters, FVector2D& SizeInMeters, FVector& RotationAxis, float& RotationDeltaInDeg);
    
private:
    UFUNCTION(BlueprintCallable)
    static EGearVRControllerHandedness_DEPRECATED GetGearVRControllerHandedness();
    
    UFUNCTION(BlueprintCallable)
    static float GetBatteryLevel();
    
public:
    UFUNCTION(BlueprintCallable)
    static void GetBaseRotationAndPositionOffset(FRotator& OutRot, FVector& OutPosOffset);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static void GetBaseRotationAndBaseOffsetInMeters(FRotator& OutRotation, FVector& OutBaseOffsetInMeters);
    
    UFUNCTION(BlueprintCallable)
    static void EnableAutoLoadingSplashScreen(bool bAutoShowEnabled);
    
private:
    UFUNCTION(BlueprintCallable)
    static void EnableArmModel(bool bArmModelEnable);
    
public:
    UFUNCTION(BlueprintCallable)
    static void ClearLoadingSplashScreens();
    
private:
    UFUNCTION(BlueprintCallable)
    static bool AreHeadPhonesPluggedIn();
    
public:
    UFUNCTION(BlueprintCallable)
    static void AddLoadingSplashScreen(UTexture2D* Texture, FVector TranslationInMeters, FRotator Rotation, FVector2D SizeInMeters, FRotator DeltaRotation, bool bClearBeforeAdd);
    
};

