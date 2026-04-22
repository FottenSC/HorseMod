#include "MaterialExpressionTextureSampleParameter.h"

UMaterialExpressionTextureSampleParameter::UMaterialExpressionTextureSampleParameter() {
    this->bIsParameterExpression = true;
    this->Outputs.AddDefaulted(5);
}


