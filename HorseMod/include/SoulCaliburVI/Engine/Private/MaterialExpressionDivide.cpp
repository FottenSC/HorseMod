#include "MaterialExpressionDivide.h"

UMaterialExpressionDivide::UMaterialExpressionDivide() {
    this->Outputs.AddDefaulted(1);
    this->ConstA = 1.00f;
    this->ConstB = 2.00f;
}


