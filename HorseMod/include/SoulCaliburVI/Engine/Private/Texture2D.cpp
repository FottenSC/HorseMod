#include "Texture2D.h"

UTexture2D::UTexture2D() {
    this->RequestedMips = 0;
    this->ResidentMips = 0;
    this->StreamingIndex = -1;
    this->LevelIndex = -1;
    this->FirstResourceMemMip = 0;
    this->ForceMipLevelsToBeResidentTimestamp = 0.00f;
    this->bTemporarilyDisableStreaming = false;
    this->bIsStreamable = false;
    this->bHasStreamingUpdatePending = false;
    this->bHasCancelationPending = false;
    this->bForceMiplevelsToBeResident = false;
    this->bIgnoreStreamingMipBias = false;
    this->bGlobalForceMipLevelsToBeResident = false;
    this->AddressX = TA_Wrap;
    this->AddressY = TA_Wrap;
}

int32 UTexture2D::Blueprint_GetSizeY() const {
    return 0;
}

int32 UTexture2D::Blueprint_GetSizeX() const {
    return 0;
}


