#include "MovieSceneEasingSettings.h"

FMovieSceneEasingSettings::FMovieSceneEasingSettings() {
    this->AutoEaseInTime = 0.00f;
    this->AutoEaseOutTime = 0.00f;
    this->bManualEaseIn = false;
    this->ManualEaseInTime = 0.00f;
    this->bManualEaseOut = false;
    this->ManualEaseOutTime = 0.00f;
}

