#include "MaterialExpressionBreakMaterialAttributes.h"

UMaterialExpressionBreakMaterialAttributes::UMaterialExpressionBreakMaterialAttributes() {
    this->bShowOutputNameOnPin = true;
    this->bShowMaskColorsOnPin = false;
    this->Outputs.AddDefaulted(25);
}


