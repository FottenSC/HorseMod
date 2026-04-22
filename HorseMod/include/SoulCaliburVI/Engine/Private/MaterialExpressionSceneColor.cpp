#include "MaterialExpressionSceneColor.h"

UMaterialExpressionSceneColor::UMaterialExpressionSceneColor() {
    this->bShaderInputData = true;
    this->Outputs.AddDefaulted(1);
    this->InputMode = EMaterialSceneAttributeInputMode::Coordinates;
}


