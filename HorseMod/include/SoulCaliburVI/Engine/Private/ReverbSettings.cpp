#include "ReverbSettings.h"

FReverbSettings::FReverbSettings() {
    this->bApplyReverb = false;
    this->ReverbType = REVERB_Default;
    this->ReverbEffect = NULL;
    this->Volume = 0.00f;
    this->FadeTime = 0.00f;
}

