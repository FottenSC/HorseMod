#include "MovieSceneEvalTemplate.h"

FMovieSceneEvalTemplate::FMovieSceneEvalTemplate() {
    this->CompletionMode = EMovieSceneCompletionMode::KeepState;
    this->SourceSection = NULL;
}

