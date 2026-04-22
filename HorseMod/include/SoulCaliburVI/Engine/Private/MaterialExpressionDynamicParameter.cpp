#include "MaterialExpressionDynamicParameter.h"

UMaterialExpressionDynamicParameter::UMaterialExpressionDynamicParameter() {
    this->bShowOutputNameOnPin = true;
    this->bHidePreviewWindow = true;
    this->bShaderInputData = true;
    this->Outputs.AddDefaulted(4);
    this->ParamNames.AddDefaulted(4);
}


