#include "MaterialExpressionAdd.h"

UMaterialExpressionAdd::UMaterialExpressionAdd() {
    this->Outputs.AddDefaulted(1);
    this->ConstA = 0.00f;
    this->ConstB = 1.00f;
}


