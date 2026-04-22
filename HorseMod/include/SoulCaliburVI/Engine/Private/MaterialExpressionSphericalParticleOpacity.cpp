#include "MaterialExpressionSphericalParticleOpacity.h"

UMaterialExpressionSphericalParticleOpacity::UMaterialExpressionSphericalParticleOpacity() {
    this->bCollapsed = false;
    this->Outputs.AddDefaulted(1);
    this->ConstantDensity = 1.00f;
}


