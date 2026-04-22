#include "MaterialExpressionFunctionOutput.h"

UMaterialExpressionFunctionOutput::UMaterialExpressionFunctionOutput() {
    this->bCollapsed = false;
    this->bShowOutputs = false;
    this->Outputs.AddDefaulted(1);
    this->OutputName = TEXT("Result");
    this->SortPriority = 0;
    this->bLastPreviewed = false;
}


