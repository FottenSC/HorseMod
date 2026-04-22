#include "MaterialExpressionParameter.h"

UMaterialExpressionParameter::UMaterialExpressionParameter() {
    this->bIsParameterExpression = true;
    this->bCollapsed = false;
    this->Outputs.AddDefaulted(1);
}


