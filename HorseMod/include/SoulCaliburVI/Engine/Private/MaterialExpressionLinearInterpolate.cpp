#include "MaterialExpressionLinearInterpolate.h"

UMaterialExpressionLinearInterpolate::UMaterialExpressionLinearInterpolate() {
    this->Outputs.AddDefaulted(1);
    this->ConstA = 0.00f;
    this->ConstB = 1.00f;
    this->ConstAlpha = 0.50f;
}


