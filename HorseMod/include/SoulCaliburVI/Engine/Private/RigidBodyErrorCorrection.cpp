#include "RigidBodyErrorCorrection.h"

FRigidBodyErrorCorrection::FRigidBodyErrorCorrection() {
    this->LinearDeltaThresholdSq = 0.00f;
    this->LinearInterpAlpha = 0.00f;
    this->LinearRecipFixTime = 0.00f;
    this->AngularDeltaThreshold = 0.00f;
    this->AngularInterpAlpha = 0.00f;
    this->AngularRecipFixTime = 0.00f;
    this->BodySpeedThresholdSq = 0.00f;
}

