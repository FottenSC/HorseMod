#include "MaterialExpressionPanner.h"

UMaterialExpressionPanner::UMaterialExpressionPanner() {
    this->Outputs.AddDefaulted(1);
    this->SpeedX = 0.00f;
    this->SpeedY = 0.00f;
    this->ConstCoordinate = 0;
    this->bFractionalPart = false;
}


