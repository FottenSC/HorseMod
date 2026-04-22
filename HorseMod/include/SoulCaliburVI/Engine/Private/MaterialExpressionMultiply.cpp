#include "MaterialExpressionMultiply.h"

UMaterialExpressionMultiply::UMaterialExpressionMultiply() {
    this->Outputs.AddDefaulted(1);
    this->ConstA = 0.00f;
    this->ConstB = 1.00f;
}


