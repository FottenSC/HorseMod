#include "MaterialExpressionDepthOfFieldFunction.h"

UMaterialExpressionDepthOfFieldFunction::UMaterialExpressionDepthOfFieldFunction() {
    this->bCollapsed = false;
    this->Outputs.AddDefaulted(1);
    this->FunctionValue = TDOF_NearAndFarMask;
}


