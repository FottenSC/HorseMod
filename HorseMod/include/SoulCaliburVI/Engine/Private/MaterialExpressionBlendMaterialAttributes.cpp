#include "MaterialExpressionBlendMaterialAttributes.h"

UMaterialExpressionBlendMaterialAttributes::UMaterialExpressionBlendMaterialAttributes() {
    this->Outputs.AddDefaulted(1);
    this->PixelAttributeBlendType = EMaterialAttributeBlend::Blend;
    this->VertexAttributeBlendType = EMaterialAttributeBlend::Blend;
}


