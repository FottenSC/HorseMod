#include "MaterialExpressionTextureBase.h"

UMaterialExpressionTextureBase::UMaterialExpressionTextureBase() {
    this->Outputs.AddDefaulted(1);
    this->Texture = NULL;
    this->SamplerType = SAMPLERTYPE_Color;
    this->IsDefaultMeshpaintTexture = false;
}


