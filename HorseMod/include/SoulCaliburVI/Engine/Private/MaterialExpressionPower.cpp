#include "MaterialExpressionPower.h"

UMaterialExpressionPower::UMaterialExpressionPower() {
    this->Outputs.AddDefaulted(1);
    this->ConstExponent = 2.00f;
}


