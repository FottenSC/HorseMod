#include "MaterialExpressionTangent.h"

UMaterialExpressionTangent::UMaterialExpressionTangent() {
    this->Outputs.AddDefaulted(1);
    this->Period = 1.00f;
}


