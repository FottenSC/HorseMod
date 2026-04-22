#include "MovieSceneSequencePlaybackSettings.h"

FMovieSceneSequencePlaybackSettings::FMovieSceneSequencePlaybackSettings() {
    this->LoopCount = 0;
    this->PlayRate = 0.00f;
    this->bRandomStartTime = false;
    this->StartTime = 0.00f;
    this->bRestoreState = false;
    this->bDisableMovementInput = false;
    this->bDisableLookAtInput = false;
    this->bHidePlayer = false;
    this->bHideHud = false;
}

