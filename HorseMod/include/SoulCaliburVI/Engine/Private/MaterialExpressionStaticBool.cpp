#include "MaterialExpressionStaticBool.h"

UMaterialExpressionStaticBool::UMaterialExpressionStaticBool() {
    this->bHidePreviewWindow = true;
    this->bShaderInputData = true;
    this->Outputs.AddDefaulted(1);
    this->Value = false;
}


