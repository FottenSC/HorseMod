#include "MaterialExpressionViewProperty.h"

UMaterialExpressionViewProperty::UMaterialExpressionViewProperty() {
    this->bShowOutputNameOnPin = true;
    this->bShaderInputData = true;
    this->Outputs.AddDefaulted(2);
    this->Property = MEVP_FieldOfView;
}


