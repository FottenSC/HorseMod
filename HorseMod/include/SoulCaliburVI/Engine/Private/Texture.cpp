#include "Texture.h"

UTexture::UTexture() {
    this->LODBias = 0;
    this->NumCinematicMipLevels = 0;
    this->SRGB = true;
    this->NeverStream = false;
    this->bNoTiling = false;
    this->bUseCinematicMipLevels = false;
    this->CachedCombinedLODBias = 0;
    this->bAsyncResourceReleaseHasBeenStarted = false;
    this->CompressionSettings = TC_Default;
    this->Filter = TF_Default;
    this->LODGroup = TEXTUREGROUP_World;
}


