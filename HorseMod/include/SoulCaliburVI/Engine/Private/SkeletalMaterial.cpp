#include "SkeletalMaterial.h"

FSkeletalMaterial::FSkeletalMaterial() {
    this->MaterialInterface = NULL;
    this->bEnableShadowCasting = false;
    this->bRecomputeTangent = false;
}

