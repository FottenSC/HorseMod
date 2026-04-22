#include "MaterialExpressionDepthFade.h"

UMaterialExpressionDepthFade::UMaterialExpressionDepthFade() {
    this->bCollapsed = false;
    this->Outputs.AddDefaulted(1);
    this->OpacityDefault = 1.00f;
    this->FadeDistanceDefault = 100.00f;
}


