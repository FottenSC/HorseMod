#include "MaterialExpressionSphereMask.h"

UMaterialExpressionSphereMask::UMaterialExpressionSphereMask() {
    this->Outputs.AddDefaulted(1);
    this->AttenuationRadius = 256.00f;
    this->HardnessPercent = 100.00f;
}


