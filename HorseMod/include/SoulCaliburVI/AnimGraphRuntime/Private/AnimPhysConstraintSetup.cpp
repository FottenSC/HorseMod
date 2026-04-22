#include "AnimPhysConstraintSetup.h"

FAnimPhysConstraintSetup::FAnimPhysConstraintSetup() {
    this->LinearXLimitType = AnimPhysLinearConstraintType::Free;
    this->LinearYLimitType = AnimPhysLinearConstraintType::Free;
    this->LinearZLimitType = AnimPhysLinearConstraintType::Free;
    this->AngularConstraintType = AnimPhysAngularConstraintType::Angular;
    this->TwistAxis = AnimPhysTwistAxis::AxisX;
    this->ConeAngle = 0.00f;
    this->AngularXAngle = 0.00f;
    this->AngularYAngle = 0.00f;
    this->AngularZAngle = 0.00f;
    this->AngularTargetAxis = AnimPhysTwistAxis::AxisX;
    this->bLinearFullyLocked = false;
}

