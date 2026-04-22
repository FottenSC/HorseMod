#include "AlphaBlend.h"

FAlphaBlend::FAlphaBlend() {
    this->BlendOption = EAlphaBlendOption::Linear;
    this->CustomCurve = NULL;
    this->BlendTime = 0.00f;
}

