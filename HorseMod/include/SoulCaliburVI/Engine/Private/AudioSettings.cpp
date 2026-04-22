#include "AudioSettings.h"

UAudioSettings::UAudioSettings() {
    this->LowPassFilterResonance = 0.90f;
    this->MaximumConcurrentStreams = 2;
    this->QualityLevels.AddDefaulted(1);
    this->bAllowVirtualizedSounds = true;
    this->bDisableMasterEQ = false;
    this->bDisableMasterReverb = false;
    this->bAllowCenterChannel3DPanning = false;
    this->DialogueFilenameFormat = TEXT("{DialogueGuid}_{ContextId}");
}


