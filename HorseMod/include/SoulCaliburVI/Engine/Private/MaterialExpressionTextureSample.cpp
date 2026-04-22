#include "MaterialExpressionTextureSample.h"

UMaterialExpressionTextureSample::UMaterialExpressionTextureSample() {
    this->bCollapsed = false;
    this->Outputs.AddDefaulted(5);
    this->MipValueMode = TMVM_None;
    this->SamplerSource = SSM_FromTextureAsset;
    this->ConstCoordinate = 0;
    this->ConstMipValue = -1;
}


