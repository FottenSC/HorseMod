#include "MovieSceneSpawnable.h"

FMovieSceneSpawnable::FMovieSceneSpawnable() {
    this->ObjectTemplate = NULL;
    this->Ownership = ESpawnOwnership::InnerSequence;
}

