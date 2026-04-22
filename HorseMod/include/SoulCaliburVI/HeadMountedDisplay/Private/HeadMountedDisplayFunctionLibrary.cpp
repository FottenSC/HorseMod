#include "HeadMountedDisplayFunctionLibrary.h"

UHeadMountedDisplayFunctionLibrary::UHeadMountedDisplayFunctionLibrary() {
}

void UHeadMountedDisplayFunctionLibrary::SetWorldToMetersScale(UObject* WorldContext, float NewScale) {
}

void UHeadMountedDisplayFunctionLibrary::SetTrackingOrigin(TEnumAsByte<EHMDTrackingOrigin::Type> Origin) {
}

void UHeadMountedDisplayFunctionLibrary::SetSpectatorScreenTexture(UTexture* inTexture) {
}

void UHeadMountedDisplayFunctionLibrary::SetSpectatorScreenModeTexturePlusEyeLayout(FVector2D EyeRectMin, FVector2D EyeRectMax, FVector2D TextureRectMin, FVector2D TextureRectMax, bool bDrawEyeFirst, bool bClearBlack) {
}

void UHeadMountedDisplayFunctionLibrary::SetSpectatorScreenMode(ESpectatorScreenMode mode) {
}

void UHeadMountedDisplayFunctionLibrary::SetClippingPlanes(float Near, float Far) {
}

void UHeadMountedDisplayFunctionLibrary::ResetOrientationAndPosition(float Yaw, TEnumAsByte<EOrientPositionSelector::Type> Options) {
}

bool UHeadMountedDisplayFunctionLibrary::IsSpectatorScreenModeControllable() {
    return false;
}

bool UHeadMountedDisplayFunctionLibrary::IsInLowPersistenceMode() {
    return false;
}

bool UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled() {
    return false;
}

bool UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayConnected() {
    return false;
}

bool UHeadMountedDisplayFunctionLibrary::HasValidTrackingPosition() {
    return false;
}

float UHeadMountedDisplayFunctionLibrary::GetWorldToMetersScale(UObject* WorldContext) {
    return 0.0f;
}

void UHeadMountedDisplayFunctionLibrary::GetVRFocusState(bool& bUseFocus, bool& bHasFocus) {
}

void UHeadMountedDisplayFunctionLibrary::GetTrackingSensorParameters(FVector& Origin, FRotator& Rotation, float& LeftFOV, float& RightFOV, float& TopFOV, float& BottomFOV, float& Distance, float& NearPlane, float& FarPlane, bool& IsActive, int32 index) {
}

TEnumAsByte<EHMDTrackingOrigin::Type> UHeadMountedDisplayFunctionLibrary::GetTrackingOrigin() {
    return EHMDTrackingOrigin::Floor;
}

float UHeadMountedDisplayFunctionLibrary::GetScreenPercentage() {
    return 0.0f;
}

void UHeadMountedDisplayFunctionLibrary::GetPositionalTrackingCameraParameters(FVector& CameraOrigin, FRotator& cameraRotation, float& HFOV, float& VFOV, float& CameraDistance, float& NearPlane, float& FarPlane) {
}

void UHeadMountedDisplayFunctionLibrary::GetOrientationAndPosition(FRotator& DeviceRotation, FVector& DevicePosition) {
}

int32 UHeadMountedDisplayFunctionLibrary::GetNumOfTrackingSensors() {
    return 0;
}

TEnumAsByte<EHMDWornState::Type> UHeadMountedDisplayFunctionLibrary::GetHMDWornState() {
    return EHMDWornState::Unknown;
}

FName UHeadMountedDisplayFunctionLibrary::GetHMDDeviceName() {
    return NAME_None;
}

void UHeadMountedDisplayFunctionLibrary::EnableLowPersistenceMode(bool bEnable) {
}

bool UHeadMountedDisplayFunctionLibrary::EnableHMD(bool bEnable) {
    return false;
}


