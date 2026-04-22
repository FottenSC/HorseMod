#include "TextureRenderTarget2D.h"

UTextureRenderTarget2D::UTextureRenderTarget2D() {
    this->SizeX = 0;
    this->SizeY = 0;
    this->AddressX = TA_Wrap;
    this->AddressY = TA_Wrap;
    this->bForceLinearGamma = true;
    this->bHDR = true;
    this->RenderTargetFormat = RTF_RGBA16f;
    this->bGPUSharedFlag = false;
    this->bAutoGenerateMips = false;
    this->OverrideFormat = PF_Unknown;
}


