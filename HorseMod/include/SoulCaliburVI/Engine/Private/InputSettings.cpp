#include "InputSettings.h"

UInputSettings::UInputSettings() {
    this->AxisConfig.AddDefaulted(21);
    this->bAltEnterTogglesFullscreen = false;
    this->bF11TogglesFullscreen = false;
    this->bUseMouseForTouch = false;
    this->bEnableMouseSmoothing = true;
    this->bEnableFOVScaling = true;
    this->FOVScale = 0.01f;
    this->DoubleClickTime = 0.20f;
    this->bCaptureMouseOnLaunch = true;
    this->DefaultViewportMouseCaptureMode = EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown;
    this->bDefaultViewportMouseLock = false;
    this->DefaultViewportMouseLockMode = EMouseLockMode::LockOnCapture;
    this->bAlwaysShowTouchInterface = false;
    this->bShowConsoleOnFourFingerTap = true;
    this->ConsoleKeys.AddDefaulted(2);
}

void UInputSettings::SaveKeyMappings() {
}

void UInputSettings::RemoveAxisMapping(const FInputAxisKeyMapping& KeyMapping, bool bForceRebuildKeymaps) {
}

void UInputSettings::RemoveActionMapping(const FInputActionKeyMapping& KeyMapping, bool bForceRebuildKeymaps) {
}

UInputSettings* UInputSettings::GetInputSettings() {
    return NULL;
}

void UInputSettings::GetAxisNames(TArray<FName>& AxisNames) const {
}

void UInputSettings::GetAxisMappingByName(const FName InAxisName, TArray<FInputAxisKeyMapping>& OutMappings) const {
}

void UInputSettings::GetActionNames(TArray<FName>& ActionNames) const {
}

void UInputSettings::GetActionMappingByName(const FName InActionName, TArray<FInputActionKeyMapping>& OutMappings) const {
}

void UInputSettings::ForceRebuildKeymaps() {
}

void UInputSettings::AddAxisMapping(const FInputAxisKeyMapping& KeyMapping, bool bForceRebuildKeymaps) {
}

void UInputSettings::AddActionMapping(const FInputActionKeyMapping& KeyMapping, bool bForceRebuildKeymaps) {
}


