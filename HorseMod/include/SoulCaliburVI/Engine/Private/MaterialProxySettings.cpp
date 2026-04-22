#include "MaterialProxySettings.h"

FMaterialProxySettings::FMaterialProxySettings() {
    this->TextureSizingType = TextureSizingType_UseSingleTextureSize;
    this->GutterSpace = 0.00f;
    this->bNormalMap = false;
    this->bMetallicMap = false;
    this->MetallicConstant = 0.00f;
    this->bRoughnessMap = false;
    this->RoughnessConstant = 0.00f;
    this->bSpecularMap = false;
    this->SpecularConstant = 0.00f;
    this->bEmissiveMap = false;
    this->bOpacityMap = false;
    this->OpacityConstant = 0.00f;
    this->bOpacityMaskMap = false;
    this->OpacityMaskConstant = 0.00f;
    this->bAmbientOcclusionMap = false;
    this->AmbientOcclusionConstant = 0.00f;
    this->MaterialMergeType = MaterialMergeType_Default;
    this->BlendMode = BLEND_Opaque;
}

