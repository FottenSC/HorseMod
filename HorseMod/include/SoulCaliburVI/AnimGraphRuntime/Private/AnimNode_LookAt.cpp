#include "AnimNode_LookAt.h"

FAnimNode_LookAt::FAnimNode_LookAt() {
    this->LookAtAxis = EAxisOption::X;
    this->bUseLookUpAxis = false;
    this->LookUpAxis = EAxisOption::X;
    this->LookAtClamp = 0.00f;
    this->InterpolationType = EInterpolationBlend::Linear;
    this->InterpolationTime = 0.00f;
    this->InterpolationTriggerThreashold = 0.00f;
}

