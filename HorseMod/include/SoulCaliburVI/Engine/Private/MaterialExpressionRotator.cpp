#include "MaterialExpressionRotator.h"

UMaterialExpressionRotator::UMaterialExpressionRotator() {
    this->Outputs.AddDefaulted(1);
    this->CenterX = 0.50f;
    this->CenterY = 0.50f;
    this->Speed = 0.25f;
    this->ConstCoordinate = 0;
}


