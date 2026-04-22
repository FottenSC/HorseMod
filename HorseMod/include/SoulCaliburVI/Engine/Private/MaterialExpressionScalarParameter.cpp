#include "MaterialExpressionScalarParameter.h"

UMaterialExpressionScalarParameter::UMaterialExpressionScalarParameter() {
    this->bCollapsed = true;
    this->Outputs.AddDefaulted(1);
    this->DefaultValue = 0.00f;
    this->SliderMin = 0.00f;
    this->SliderMax = 0.00f;
}


