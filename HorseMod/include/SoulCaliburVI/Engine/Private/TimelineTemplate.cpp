#include "TimelineTemplate.h"

UTimelineTemplate::UTimelineTemplate() {
    this->TimelineLength = 5.00f;
    this->LengthMode = TL_TimelineLength;
    this->bAutoPlay = false;
    this->bLoop = false;
    this->bReplicated = false;
    this->bValidatedAsWired = false;
    this->bIgnoreTimeDilation = false;
}


