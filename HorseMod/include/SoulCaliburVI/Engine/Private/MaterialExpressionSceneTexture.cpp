#include "MaterialExpressionSceneTexture.h"

UMaterialExpressionSceneTexture::UMaterialExpressionSceneTexture() {
    this->bShowOutputNameOnPin = true;
    this->bShaderInputData = true;
    this->Outputs.AddDefaulted(3);
    this->SceneTextureId = PPI_SceneColor;
    this->bClampUVs = true;
    this->bFiltered = false;
}


