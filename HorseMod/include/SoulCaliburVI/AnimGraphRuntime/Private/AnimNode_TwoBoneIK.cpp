#include "AnimNode_TwoBoneIK.h"

FAnimNode_TwoBoneIK::FAnimNode_TwoBoneIK() {
    this->bTakeRotationFromEffectorSpace = false;
    this->bMaintainEffectorRelRot = false;
    this->bAllowStretching = false;
    this->StartStretchRatio = 0.00f;
    this->MaxStretchScale = 0.00f;
    this->EffectorLocationSpace = BCS_WorldSpace;
    this->JointTargetLocationSpace = BCS_WorldSpace;
    this->bAllowTwist = false;
    this->bNoTwist = false;
}

