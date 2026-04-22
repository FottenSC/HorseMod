#include "MaterialExpressionBumpOffset.h"

UMaterialExpressionBumpOffset::UMaterialExpressionBumpOffset() {
    this->bCollapsed = false;
    this->Outputs.AddDefaulted(1);
    this->HeightRatio = 0.05f;
    this->ReferencePlane = 0.50f;
    this->ConstCoordinate = 0;
}


