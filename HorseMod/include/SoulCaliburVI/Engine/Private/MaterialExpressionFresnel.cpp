#include "MaterialExpressionFresnel.h"

UMaterialExpressionFresnel::UMaterialExpressionFresnel() {
    this->Outputs.AddDefaulted(1);
    this->Exponent = 5.00f;
    this->BaseReflectFraction = 0.04f;
}


