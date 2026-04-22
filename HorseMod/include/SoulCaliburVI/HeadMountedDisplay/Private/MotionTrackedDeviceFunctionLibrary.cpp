#include "MotionTrackedDeviceFunctionLibrary.h"

UMotionTrackedDeviceFunctionLibrary::UMotionTrackedDeviceFunctionLibrary() {
}

void UMotionTrackedDeviceFunctionLibrary::SetIsControllerMotionTrackingEnabledByDefault(bool Enable) {
}

bool UMotionTrackedDeviceFunctionLibrary::IsMotionTrackingEnabledForDevice(int32 PlayerIndex, EControllerHand Hand) {
    return false;
}

bool UMotionTrackedDeviceFunctionLibrary::IsMotionTrackingEnabledForComponent(const UMotionControllerComponent* MotionControllerComponent) {
    return false;
}

bool UMotionTrackedDeviceFunctionLibrary::IsMotionTrackedDeviceCountManagementNecessary() {
    return false;
}

int32 UMotionTrackedDeviceFunctionLibrary::GetMotionTrackingEnabledControllerCount() {
    return 0;
}

int32 UMotionTrackedDeviceFunctionLibrary::GetMaximumMotionTrackedControllerCount() {
    return 0;
}

bool UMotionTrackedDeviceFunctionLibrary::EnableMotionTrackingOfDevice(int32 PlayerIndex, EControllerHand Hand) {
    return false;
}

bool UMotionTrackedDeviceFunctionLibrary::EnableMotionTrackingForComponent(UMotionControllerComponent* MotionControllerComponent) {
    return false;
}

void UMotionTrackedDeviceFunctionLibrary::DisableMotionTrackingOfDevice(int32 PlayerIndex, EControllerHand Hand) {
}

void UMotionTrackedDeviceFunctionLibrary::DisableMotionTrackingOfControllersForPlayer(int32 PlayerIndex) {
}

void UMotionTrackedDeviceFunctionLibrary::DisableMotionTrackingOfAllControllers() {
}

void UMotionTrackedDeviceFunctionLibrary::DisableMotionTrackingForComponent(const UMotionControllerComponent* MotionControllerComponent) {
}


