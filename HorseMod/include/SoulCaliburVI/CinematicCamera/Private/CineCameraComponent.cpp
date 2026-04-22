#include "CineCameraComponent.h"

UCineCameraComponent::UCineCameraComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->bConstrainAspectRatio = true;
    this->CurrentFocalLength = 35.00f;
    this->CurrentAperture = 2.80f;
    this->CurrentFocusDistance = 0.00f;
    this->FilmbackPresets.AddDefaulted(13);
    this->LensPresets.AddDefaulted(9);
    this->DefaultFilmbackPresetName = TEXT("16:9 DSLR");
    this->DefaultLensPresetName = TEXT("Universal Zoom");
    this->DefaultLensFocalLength = 35.00f;
    this->DefaultLensFStop = 2.80f;
}

float UCineCameraComponent::GetVerticalFieldOfView() const {
    return 0.0f;
}

float UCineCameraComponent::GetHorizontalFieldOfView() const {
    return 0.0f;
}


