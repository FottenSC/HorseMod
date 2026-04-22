#include "OculusFunctionLibrary.h"

UOculusFunctionLibrary::UOculusFunctionLibrary() {
}

void UOculusFunctionLibrary::ShowLoadingSplashScreen() {
}

void UOculusFunctionLibrary::ShowLoadingIcon(UTexture2D* Texture) {
}

void UOculusFunctionLibrary::SetPositionScale3D(FVector PosScale3D) {
}

void UOculusFunctionLibrary::SetLoadingSplashParams(const FString& TexturePath, FVector DistanceInMeters, FVector2D SizeInMeters, FVector RotationAxis, float RotationDeltaInDeg) {
}

void UOculusFunctionLibrary::SetCPUAndGPULevels(int32 CpuLevel, int32 GPULevel) {
}

void UOculusFunctionLibrary::SetBaseRotationAndPositionOffset(FRotator BaseRot, FVector PosOffset, TEnumAsByte<EOrientPositionSelector::Type> Options) {
}

void UOculusFunctionLibrary::SetBaseRotationAndBaseOffsetInMeters(FRotator Rotation, FVector BaseOffsetInMeters, TEnumAsByte<EOrientPositionSelector::Type> Options) {
}

bool UOculusFunctionLibrary::IsPowerLevelStateThrottled() {
    return false;
}

bool UOculusFunctionLibrary::IsPowerLevelStateMinimum() {
    return false;
}

bool UOculusFunctionLibrary::IsLoadingIconEnabled() {
    return false;
}

bool UOculusFunctionLibrary::IsDeviceTracked(ETrackedDeviceType DeviceType) {
    return false;
}

bool UOculusFunctionLibrary::IsControllerActive() {
    return false;
}

bool UOculusFunctionLibrary::IsAutoLoadingSplashScreenEnabled() {
    return false;
}

void UOculusFunctionLibrary::HideLoadingSplashScreen(bool bClear) {
}

void UOculusFunctionLibrary::HideLoadingIcon() {
}

bool UOculusFunctionLibrary::GetUserProfile(FHmdUserProfile& Profile) {
    return false;
}

float UOculusFunctionLibrary::GetTemperatureInCelsius() {
    return 0.0f;
}

void UOculusFunctionLibrary::GetRawSensorData(FVector& AngularAcceleration, FVector& LinearAcceleration, FVector& AngularVelocity, FVector& LinearVelocity, float& TimeInSeconds, ETrackedDeviceType DeviceType) {
}

void UOculusFunctionLibrary::GetPose(FRotator& DeviceRotation, FVector& DevicePosition, FVector& NeckPosition, bool bUseOrienationForPlayerCamera, bool bUsePositionForPlayerCamera, const FVector PositionScale) {
}

void UOculusFunctionLibrary::GetLoadingSplashParams(FString& TexturePath, FVector& DistanceInMeters, FVector2D& SizeInMeters, FVector& RotationAxis, float& RotationDeltaInDeg) {
}

EGearVRControllerHandedness_DEPRECATED UOculusFunctionLibrary::GetGearVRControllerHandedness() {
    return EGearVRControllerHandedness_DEPRECATED::RightHanded_DEPRECATED;
}

float UOculusFunctionLibrary::GetBatteryLevel() {
    return 0.0f;
}

void UOculusFunctionLibrary::GetBaseRotationAndPositionOffset(FRotator& OutRot, FVector& OutPosOffset) {
}

void UOculusFunctionLibrary::GetBaseRotationAndBaseOffsetInMeters(FRotator& OutRotation, FVector& OutBaseOffsetInMeters) {
}

void UOculusFunctionLibrary::EnableAutoLoadingSplashScreen(bool bAutoShowEnabled) {
}

void UOculusFunctionLibrary::EnableArmModel(bool bArmModelEnable) {
}

void UOculusFunctionLibrary::ClearLoadingSplashScreens() {
}

bool UOculusFunctionLibrary::AreHeadPhonesPluggedIn() {
    return false;
}

void UOculusFunctionLibrary::AddLoadingSplashScreen(UTexture2D* Texture, FVector TranslationInMeters, FRotator Rotation, FVector2D SizeInMeters, FRotator DeltaRotation, bool bClearBeforeAdd) {
}


