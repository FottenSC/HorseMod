#include "MaterialExpression.h"

UMaterialExpression::UMaterialExpression() {
    this->Material = NULL;
    this->Function = NULL;
    this->bRealtimePreview = false;
    this->bNeedToUpdatePreview = false;
    this->bIsParameterExpression = false;
    this->bCommentBubbleVisible = false;
    this->bShowOutputNameOnPin = false;
    this->bShowMaskColorsOnPin = true;
    this->bHidePreviewWindow = false;
    this->bCollapsed = true;
    this->bShaderInputData = false;
    this->bShowInputs = true;
    this->bShowOutputs = true;
    this->Outputs.AddDefaulted(1);
}


