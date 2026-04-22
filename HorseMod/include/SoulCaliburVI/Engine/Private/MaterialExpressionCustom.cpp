#include "MaterialExpressionCustom.h"

UMaterialExpressionCustom::UMaterialExpressionCustom() {
    this->bCollapsed = false;
    this->Outputs.AddDefaulted(1);
    this->Code = TEXT("1");
    this->OutputType = CMOT_Float3;
    this->Description = TEXT("Custom");
    this->Inputs.AddDefaulted(1);
}


