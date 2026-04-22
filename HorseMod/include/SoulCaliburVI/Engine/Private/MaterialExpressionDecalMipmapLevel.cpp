#include "MaterialExpressionDecalMipmapLevel.h"

UMaterialExpressionDecalMipmapLevel::UMaterialExpressionDecalMipmapLevel() {
    this->Outputs.AddDefaulted(1);
    this->ConstWidth = 256.00f;
    this->ConstHeight = 256.00f;
}


