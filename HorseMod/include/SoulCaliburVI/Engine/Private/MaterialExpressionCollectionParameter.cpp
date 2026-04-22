#include "MaterialExpressionCollectionParameter.h"

UMaterialExpressionCollectionParameter::UMaterialExpressionCollectionParameter() {
    this->bCollapsed = false;
    this->Outputs.AddDefaulted(1);
    this->Collection = NULL;
}


