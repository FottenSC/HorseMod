#include "AnimNode_LayeredBoneBlend.h"

FAnimNode_LayeredBoneBlend::FAnimNode_LayeredBoneBlend() {
    this->bMeshSpaceRotationBlend = false;
    this->CurveBlendOption = ECurveBlendOption::MaxWeight;
    this->bBlendRootMotionBasedOnRootBone = false;
    this->bHasRelevantPoses = false;
}

