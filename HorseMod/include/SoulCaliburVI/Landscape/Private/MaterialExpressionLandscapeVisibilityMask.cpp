#include "MaterialExpressionLandscapeVisibilityMask.h"

UMaterialExpressionLandscapeVisibilityMask::UMaterialExpressionLandscapeVisibilityMask() {
    this->bIsParameterExpression = true;
    this->Outputs.AddDefaulted(1);
}


