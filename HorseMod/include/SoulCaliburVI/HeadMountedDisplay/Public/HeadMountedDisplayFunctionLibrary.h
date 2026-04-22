#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Rotator -FallbackName=Rotator
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Vector -FallbackName=Vector
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Vector2D -FallbackName=Vector2D
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=BlueprintFunctionLibrary -FallbackName=BlueprintFunctionLibrary
#include "EHMDTrackingOrigin.h"
#include "EHMDWornState.h"
#include "EOrientPositionSelector.h"
#include "ESpectatorScreenMode.h"
#include "HeadMountedDisplayFunctionLibrary.generated.h"

class UObject;
class UTexture;

UCLASS(Blueprintable)
class HEADMOUNTEDDISPLAY_API UHeadMountedDisplayFunctionLibrary : public UBlueprintFunctionLibrary {
    GENERATED_BODY()
public:
    UHeadMountedDisplayFunctionLibrary();

    UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContext"))
    static void SetWorldToMetersScale(UObject* WorldContext, float NewScale);
    
    UFUNCTION(BlueprintCallable)
    static void SetTrackingOrigin(TEnumAsByte<EHMDTrackingOrigin::Type> Origin);
    
    UFUNCTION(BlueprintCallable)
    static void SetSpectatorScreenTexture(UTexture* inTexture);
    
    UFUNCTION(BlueprintCallable)
    static void SetSpectatorScreenModeTexturePlusEyeLayout(FVector2D EyeRectMin, FVector2D EyeRectMax, FVector2D TextureRectMin, FVector2D TextureRectMax, bool bDrawEyeFirst, bool bClearBlack);
    
    UFUNCTION(BlueprintCallable)
    static void SetSpectatorScreenMode(ESpectatorScreenMode mode);
    
    UFUNCTION(BlueprintCallable)
    static void SetClippingPlanes(float Near, float Far);
    
    UFUNCTION(BlueprintCallable)
    static void ResetOrientationAndPosition(float Yaw, TEnumAsByte<EOrientPositionSelector::Type> Options);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool IsSpectatorScreenModeControllable();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool IsInLowPersistenceMode();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool IsHeadMountedDisplayEnabled();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool IsHeadMountedDisplayConnected();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool HasValidTrackingPosition();
    
    UFUNCTION(BlueprintCallable, BlueprintPure, meta=(WorldContext="WorldContext"))
    static float GetWorldToMetersScale(UObject* WorldContext);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static void GetVRFocusState(bool& bUseFocus, bool& bHasFocus);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static void GetTrackingSensorParameters(FVector& Origin, FRotator& Rotation, float& LeftFOV, float& RightFOV, float& TopFOV, float& BottomFOV, float& Distance, float& NearPlane, float& FarPlane, bool& IsActive, int32 index);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static TEnumAsByte<EHMDTrackingOrigin::Type> GetTrackingOrigin();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static float GetScreenPercentage();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static void GetPositionalTrackingCameraParameters(FVector& CameraOrigin, FRotator& cameraRotation, float& HFOV, float& VFOV, float& CameraDistance, float& NearPlane, float& FarPlane);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static void GetOrientationAndPosition(FRotator& DeviceRotation, FVector& DevicePosition);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 GetNumOfTrackingSensors();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static TEnumAsByte<EHMDWornState::Type> GetHMDWornState();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static FName GetHMDDeviceName();
    
    UFUNCTION(BlueprintCallable)
    static void EnableLowPersistenceMode(bool bEnable);
    
    UFUNCTION(BlueprintCallable)
    static bool EnableHMD(bool bEnable);
    
};

