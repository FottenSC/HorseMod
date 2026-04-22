#include "MaterialExpressionConstantBiasScale.h"

UMaterialExpressionConstantBiasScale::UMaterialExpressionConstantBiasScale() {
    this->Outputs.AddDefaulted(1);
    this->Bias = 1.00f;
    this->Scale = 0.50f;
}


