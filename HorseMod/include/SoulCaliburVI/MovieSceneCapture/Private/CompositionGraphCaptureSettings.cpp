#include "CompositionGraphCaptureSettings.h"

UCompositionGraphCaptureSettings::UCompositionGraphCaptureSettings() {
    this->bCaptureFramesInHDR = false;
    this->HDRCompressionQuality = 0;
    this->CaptureGamut = HCGM_Rec709;
}


