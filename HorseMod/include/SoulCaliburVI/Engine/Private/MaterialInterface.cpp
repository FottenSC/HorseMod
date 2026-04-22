#include "MaterialInterface.h"

UMaterialInterface::UMaterialInterface() {
    this->SubsurfaceProfile = NULL;
}

UPhysicalMaterial* UMaterialInterface::GetPhysicalMaterial() const {
    return NULL;
}

UMaterial* UMaterialInterface::GetBaseMaterial() {
    return NULL;
}


