#include "MaterialExpressionSpeedTree.h"

UMaterialExpressionSpeedTree::UMaterialExpressionSpeedTree() {
    this->Outputs.AddDefaulted(1);
    this->GeometryType = STG_Branch;
    this->WindType = STW_None;
    this->LODType = STLOD_Pop;
    this->BillboardThreshold = 0.90f;
    this->bAccurateWindVelocities = false;
}


