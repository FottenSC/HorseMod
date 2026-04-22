#include "MaterialInstanceBasePropertyOverrides.h"

FMaterialInstanceBasePropertyOverrides::FMaterialInstanceBasePropertyOverrides() {
    this->bOverride_OpacityMaskClipValue = false;
    this->bOverride_BlendMode = false;
    this->bOverride_ShadingModel = false;
    this->bOverride_DitheredLODTransition = false;
    this->bOverride_TwoSided = false;
    this->OpacityMaskClipValue = 0.00f;
    this->BlendMode = BLEND_Opaque;
    this->ShadingModel = MSM_Unlit;
    this->TwoSided = false;
    this->DitheredLODTransition = false;
}

