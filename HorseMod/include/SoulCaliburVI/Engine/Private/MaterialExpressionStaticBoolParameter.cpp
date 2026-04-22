#include "MaterialExpressionStaticBoolParameter.h"

UMaterialExpressionStaticBoolParameter::UMaterialExpressionStaticBoolParameter() {
    this->bHidePreviewWindow = true;
    this->Outputs.AddDefaulted(1);
    this->DefaultValue = false;
}


