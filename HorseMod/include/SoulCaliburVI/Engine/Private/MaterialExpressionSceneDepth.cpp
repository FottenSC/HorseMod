#include "MaterialExpressionSceneDepth.h"

UMaterialExpressionSceneDepth::UMaterialExpressionSceneDepth() {
    this->bShaderInputData = true;
    this->Outputs.AddDefaulted(1);
    this->InputMode = EMaterialSceneAttributeInputMode::Coordinates;
}


