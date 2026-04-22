#include "MaterialExpressionLandscapeLayerCoords.h"

UMaterialExpressionLandscapeLayerCoords::UMaterialExpressionLandscapeLayerCoords() {
    this->bCollapsed = false;
    this->Outputs.AddDefaulted(1);
    this->MappingType = TCMT_Auto;
    this->CustomUVType = LCCT_None;
    this->MappingScale = 0.00f;
    this->MappingRotation = 0.00f;
    this->MappingPanU = 0.00f;
    this->MappingPanV = 0.00f;
}


