#include "SlateBrush.h"

FSlateBrush::FSlateBrush() {
    this->DrawAs = ESlateBrushDrawType::NoDrawType;
    this->Tiling = ESlateBrushTileType::NoTile;
    this->Mirroring = ESlateBrushMirrorType::NoMirror;
    this->ImageType = ESlateBrushImageType::NoImage;
    this->ResourceObject = NULL;
    this->bIsDynamicallyLoaded = false;
    this->bHasUObject = false;
}

