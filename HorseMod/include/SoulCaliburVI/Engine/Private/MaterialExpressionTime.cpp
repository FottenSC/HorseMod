#include "MaterialExpressionTime.h"

UMaterialExpressionTime::UMaterialExpressionTime() {
    this->bShaderInputData = true;
    this->Outputs.AddDefaulted(1);
    this->bIgnorePause = false;
    this->bOverride_Period = false;
    this->Period = 0.00f;
}


