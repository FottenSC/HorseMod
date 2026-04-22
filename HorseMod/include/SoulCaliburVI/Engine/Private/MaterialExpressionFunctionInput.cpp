#include "MaterialExpressionFunctionInput.h"

UMaterialExpressionFunctionInput::UMaterialExpressionFunctionInput() {
    this->bCollapsed = false;
    this->Outputs.AddDefaulted(1);
    this->InputName = TEXT("In");
    this->InputType = FunctionInput_Vector3;
    this->bUsePreviewValueAsDefault = false;
    this->SortPriority = 0;
    this->bCompilingFunctionPreview = true;
}


