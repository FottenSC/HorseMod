#include "MaterialExpressionDecalDerivative.h"

UMaterialExpressionDecalDerivative::UMaterialExpressionDecalDerivative() {
    this->bShowOutputNameOnPin = true;
    this->bShaderInputData = true;
    this->Outputs.AddDefaulted(2);
}


