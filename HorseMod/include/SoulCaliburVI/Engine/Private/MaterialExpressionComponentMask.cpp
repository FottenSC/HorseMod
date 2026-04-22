#include "MaterialExpressionComponentMask.h"

UMaterialExpressionComponentMask::UMaterialExpressionComponentMask() {
    this->Outputs.AddDefaulted(1);
    this->R = false;
    this->G = false;
    this->B = false;
    this->A = false;
}


