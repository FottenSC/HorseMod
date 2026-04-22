#include "MaterialExpressionPixelDepth.h"

UMaterialExpressionPixelDepth::UMaterialExpressionPixelDepth() {
    this->bShaderInputData = true;
    this->Outputs.AddDefaulted(1);
}


