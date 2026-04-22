#include "AnselFunctionLibrary.h"

UAnselFunctionLibrary::UAnselFunctionLibrary() {
}

void UAnselFunctionLibrary::StopSessionDev(UObject* WorldContextObject) {
}

void UAnselFunctionLibrary::StopSession(UObject* WorldContextObject) {
}

void UAnselFunctionLibrary::StartSessionDev(UObject* WorldContextObject) {
}

void UAnselFunctionLibrary::StartSession(UObject* WorldContextObject) {
}

void UAnselFunctionLibrary::SetUIControlVisibility(UObject* WorldContextObject, const TEnumAsByte<EUIControlEffectTarget> UIControlTarget, const bool bIsVisible) {
}

void UAnselFunctionLibrary::SetSettleFrames(const int32 NumSettleFrames) {
}

void UAnselFunctionLibrary::SetIsPhotographyAllowed(const bool bIsPhotographyAllowed) {
}

void UAnselFunctionLibrary::SetCameraMovementSpeed(const float TranslationSpeed) {
}

void UAnselFunctionLibrary::SetCameraConstraintDistance(const float MaxCameraDistance) {
}

void UAnselFunctionLibrary::SetCameraConstraintCameraSize(const float CameraSize) {
}

void UAnselFunctionLibrary::SetAutoPostprocess(const bool bShouldAutoPostprocess) {
}

void UAnselFunctionLibrary::SetAutoPause(const bool bShouldAutoPause) {
}

bool UAnselFunctionLibrary::IsPhotographyAvailable() {
    return false;
}

bool UAnselFunctionLibrary::IsPhotographyAllowed() {
    return false;
}

void UAnselFunctionLibrary::ConstrainCameraByGeometry(UObject* WorldContextObject, const FVector NewCameraLocation, const FVector PreviousCameraLocation, const FVector OriginalCameraLocation, FVector& OutCameraLocation) {
}

void UAnselFunctionLibrary::ConstrainCameraByDistance(UObject* WorldContextObject, const FVector NewCameraLocation, const FVector PreviousCameraLocation, const FVector OriginalCameraLocation, FVector& OutCameraLocation, float MaxDistance) {
}


