#include "MaterialExpressionConstant.h"

UMaterialExpressionConstant::UMaterialExpressionConstant() {
    this->Outputs.AddDefaulted(1);
    this->R = 0.00f;
}


