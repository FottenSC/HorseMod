#include "MaterialExpressionSine.h"

UMaterialExpressionSine::UMaterialExpressionSine() {
    this->Outputs.AddDefaulted(1);
    this->Period = 1.00f;
}


