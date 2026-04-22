#include "Slider.h"

USlider::USlider() {
    this->Value = 0.00f;
    this->Orientation = Orient_Horizontal;
    this->IndentHandle = false;
    this->Locked = false;
    this->StepSize = 0.01f;
    this->IsFocusable = true;
}

void USlider::SetValue(float inValue) {
}

void USlider::SetStepSize(float inValue) {
}

void USlider::SetSliderHandleColor(FLinearColor inValue) {
}

void USlider::SetSliderBarColor(FLinearColor inValue) {
}

void USlider::SetLocked(bool inValue) {
}

void USlider::SetIndentHandle(bool inValue) {
}

float USlider::GetValue() const {
    return 0.0f;
}


