#include "MovieSceneAudioSection.h"

UMovieSceneAudioSection::UMovieSceneAudioSection() {
    this->Sound = NULL;
    this->StartOffset = 0.00f;
    this->AudioStartTime = 340282346638528859811704183484516925440.00f;
    this->AudioDilationFactor = 340282346638528859811704183484516925440.00f;
    this->AudioVolume = 340282346638528859811704183484516925440.00f;
    this->bSuppressSubtitles = false;
    this->bOverrideAttenuation = false;
    this->AttenuationSettings = NULL;
}


