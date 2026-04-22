#include "MaterialExpressionFontSample.h"

UMaterialExpressionFontSample::UMaterialExpressionFontSample() {
    this->bCollapsed = false;
    this->Outputs.AddDefaulted(5);
    this->Font = NULL;
    this->FontTexturePage = 0;
}


