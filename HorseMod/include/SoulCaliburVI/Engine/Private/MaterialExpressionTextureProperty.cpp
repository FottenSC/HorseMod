#include "MaterialExpressionTextureProperty.h"

UMaterialExpressionTextureProperty::UMaterialExpressionTextureProperty() {
    this->bShaderInputData = true;
    this->Outputs.AddDefaulted(1);
    this->Property = TMTM_TextureSize;
}


