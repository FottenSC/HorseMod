#include "AnimNode_Fabrik.h"

FAnimNode_Fabrik::FAnimNode_Fabrik() {
    this->EffectorTransformSpace = BCS_WorldSpace;
    this->EffectorRotationSource = BRS_KeepComponentSpaceRotation;
    this->Precision = 0.00f;
    this->MaxIterations = 0;
    this->bEnableDebugDraw = false;
}

