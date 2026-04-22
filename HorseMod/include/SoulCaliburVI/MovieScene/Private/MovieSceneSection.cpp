#include "MovieSceneSection.h"

UMovieSceneSection::UMovieSceneSection() {
    this->StartTime = 0.00f;
    this->EndTime = 0.00f;
    this->RowIndex = 0;
    this->OverlapPriority = 0;
    this->bIsActive = true;
    this->bIsLocked = false;
    this->bIsInfinite = false;
    this->PrerollTime = 0.00f;
    this->PostrollTime = 0.00f;
}


