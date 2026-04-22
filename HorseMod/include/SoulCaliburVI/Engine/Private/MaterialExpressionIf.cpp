#include "MaterialExpressionIf.h"

UMaterialExpressionIf::UMaterialExpressionIf() {
    this->Outputs.AddDefaulted(1);
    this->EqualsThreshold = 0.00f;
    this->ConstB = 0.00f;
    this->ConstAEqualsB = 0.00f;
}


