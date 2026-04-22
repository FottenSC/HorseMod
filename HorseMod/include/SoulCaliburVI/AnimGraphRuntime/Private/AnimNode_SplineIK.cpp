#include "AnimNode_SplineIK.h"

FAnimNode_SplineIK::FAnimNode_SplineIK() {
    this->BoneAxis = (ESplineBoneAxis)0;
    this->bAutoCalculateSpline = false;
    this->PointCount = 0;
    this->Roll = 0.00f;
    this->TwistStart = 0.00f;
    this->TwistEnd = 0.00f;
    this->Stretch = 0.00f;
    this->Offset = 0.00f;
    this->OriginalSplineLength = 0.00f;
}

