#include "MaterialExpressionLightmapUVs.h"

UMaterialExpressionLightmapUVs::UMaterialExpressionLightmapUVs() {
    this->bShowOutputNameOnPin = true;
    this->bHidePreviewWindow = true;
    this->bShaderInputData = true;
    this->Outputs.AddDefaulted(1);
}


