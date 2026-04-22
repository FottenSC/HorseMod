#include "SoundWave.h"

USoundWave::USoundWave() {
    this->CompressionQuality = 40;
    this->bLooping = false;
    this->bStreaming = false;
    this->StreamingPriority = 0;
    this->bMature = false;
    this->bManualWordWrap = false;
    this->bSingleLine = false;
    this->bVirtualizeWhenSilent = false;
    this->SoundGroup = SOUNDGROUP_Default;
    this->SubtitlePriority = 10000.00f;
    this->Volume = 1.00f;
    this->Pitch = 1.00f;
    this->NumChannels = 0;
    this->SampleRate = 0;
    this->RawPCMDataSize = 0;
    this->Curves = NULL;
    this->InternalCurves = NULL;
}


