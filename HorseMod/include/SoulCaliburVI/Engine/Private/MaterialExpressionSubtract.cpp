#include "MaterialExpressionSubtract.h"

UMaterialExpressionSubtract::UMaterialExpressionSubtract() {
    this->Outputs.AddDefaulted(1);
    this->ConstA = 1.00f;
    this->ConstB = 1.00f;
}


