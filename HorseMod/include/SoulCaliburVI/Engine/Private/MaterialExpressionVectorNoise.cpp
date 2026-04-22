#include "MaterialExpressionVectorNoise.h"

UMaterialExpressionVectorNoise::UMaterialExpressionVectorNoise() {
    this->Outputs.AddDefaulted(1);
    this->NoiseFunction = VNF_CellnoiseALU;
    this->Quality = 1;
    this->bTiling = false;
    this->TileSize = 300;
}


