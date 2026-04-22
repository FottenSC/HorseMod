#include "GrassVariety.h"

FGrassVariety::FGrassVariety() {
    this->GrassMesh = NULL;
    this->GrassDensity = 0.00f;
    this->bUseGrid = false;
    this->PlacementJitter = 0.00f;
    this->StartCullDistance = 0;
    this->EndCullDistance = 0;
    this->MinLOD = 0;
    this->Scaling = EGrassScaling::Uniform;
    this->RandomRotation = false;
    this->AlignToSurface = false;
    this->bUseLandscapeLightmap = false;
    this->bReceivesDecals = false;
}

