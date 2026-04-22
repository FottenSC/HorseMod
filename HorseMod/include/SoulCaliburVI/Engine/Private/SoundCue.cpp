#include "SoundCue.h"

USoundCue::USoundCue() {
    this->bOverrideAttenuation = false;
    this->FirstNode = NULL;
    this->VolumeMultiplier = 0.75f;
    this->PitchMultiplier = 1.00f;
    this->SubtitlePriority = 10000.00f;
}


