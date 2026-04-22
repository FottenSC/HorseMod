#include "MaterialExpressionReflectionVectorWS.h"

UMaterialExpressionReflectionVectorWS::UMaterialExpressionReflectionVectorWS() {
    this->bShaderInputData = true;
    this->Outputs.AddDefaulted(1);
    this->bNormalizeCustomWorldNormal = false;
}


