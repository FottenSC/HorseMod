#include "SkeletalMeshLODInfo.h"

FSkeletalMeshLODInfo::FSkeletalMeshLODInfo() {
    this->ScreenSize = 0.00f;
    this->LODHysteresis = 0.00f;
    this->bHasBeenSimplified = false;
    this->BakePose = NULL;
    this->bHasPerLODVertexColors = false;
}

