#include "MovieSceneAudioSectionTemplateData.h"

FMovieSceneAudioSectionTemplateData::FMovieSceneAudioSectionTemplateData() {
    this->Sound = NULL;
    this->AudioStartOffset = 0.00f;
    this->RowIndex = 0;
    this->bOverrideAttenuation = false;
    this->AttenuationSettings = NULL;
}

