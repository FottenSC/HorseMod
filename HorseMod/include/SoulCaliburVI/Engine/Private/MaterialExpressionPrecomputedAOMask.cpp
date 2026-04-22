#include "MaterialExpressionPrecomputedAOMask.h"

UMaterialExpressionPrecomputedAOMask::UMaterialExpressionPrecomputedAOMask() {
    this->bShowOutputNameOnPin = true;
    this->bHidePreviewWindow = true;
    this->bShaderInputData = true;
    this->Outputs.AddDefaulted(1);
}


