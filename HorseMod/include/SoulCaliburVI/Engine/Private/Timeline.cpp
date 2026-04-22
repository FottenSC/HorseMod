#include "Timeline.h"

FTimeline::FTimeline() {
    this->LengthMode = TL_TimelineLength;
    this->Length = 0.00f;
    this->bLooping = false;
    this->bReversePlayback = false;
    this->bPlaying = false;
    this->PlayRate = 0.00f;
    this->Position = 0.00f;
    this->DirectionProperty = NULL;
}

