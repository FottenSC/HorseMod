#include "MaterialExpressionLandscapeLayerSample.h"

UMaterialExpressionLandscapeLayerSample::UMaterialExpressionLandscapeLayerSample() {
    this->bIsParameterExpression = true;
    this->Outputs.AddDefaulted(1);
    this->PreviewWeight = 0.00f;
}


