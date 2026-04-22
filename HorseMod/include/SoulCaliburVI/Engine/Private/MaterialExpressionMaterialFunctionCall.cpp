#include "MaterialExpressionMaterialFunctionCall.h"

UMaterialExpressionMaterialFunctionCall::UMaterialExpressionMaterialFunctionCall() {
    this->bShowOutputNameOnPin = true;
    this->bHidePreviewWindow = true;
    this->Outputs.AddDefaulted(1);
    this->MaterialFunction = NULL;
}


