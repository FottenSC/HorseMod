#include "MaterialExpressionScreenPosition.h"

UMaterialExpressionScreenPosition::UMaterialExpressionScreenPosition() {
    this->bShaderInputData = true;
    this->Outputs.AddDefaulted(1);
    this->Mapping = MESP_SceneTextureUV;
}


