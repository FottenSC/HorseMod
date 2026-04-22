#include "MaterialExpressionWorldPosition.h"

UMaterialExpressionWorldPosition::UMaterialExpressionWorldPosition() {
    this->bShaderInputData = true;
    this->Outputs.AddDefaulted(1);
    this->WorldPositionShaderOffset = WPT_Default;
}


