#include "MaterialExpressionLandscapeLayerSwitch.h"

UMaterialExpressionLandscapeLayerSwitch::UMaterialExpressionLandscapeLayerSwitch() {
    this->bIsParameterExpression = true;
    this->bCollapsed = false;
    this->Outputs.AddDefaulted(1);
    this->PreviewUsed = true;
}


