#include "LuxCreationTextureSpec.h"

ULuxCreationTextureSpec::ULuxCreationTextureSpec() {
    this->TexturePrinters.AddDefaulted(1);
}

UTexture* ULuxCreationTextureSpec::PrintTexture(const FLuxTexturePrintParam& InParam) {
    return NULL;
}


