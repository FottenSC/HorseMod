#include "TextureLODGroup.h"

FTextureLODGroup::FTextureLODGroup() {
    this->Group = TEXTUREGROUP_World;
    this->LODBias = 0;
    this->NumStreamedMips = 0;
    this->MipGenSettings = TMGS_FromTextureGroup;
    this->MinLODSize = 0;
    this->MaxLODSize = 0;
}

