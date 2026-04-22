#include "MaterialExpressionLandscapeLayerWeight.h"

UMaterialExpressionLandscapeLayerWeight::UMaterialExpressionLandscapeLayerWeight() {
    this->bIsParameterExpression = true;
    this->Outputs.AddDefaulted(1);
    this->PreviewWeight = 0.00f;
}


