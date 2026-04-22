#include "MovieSceneCinematicShotSection.h"

UMovieSceneCinematicShotSection::UMovieSceneCinematicShotSection() {
    const FProperty* p_PrerollTime = GetClass()->FindPropertyByName("PrerollTime");
    (*p_PrerollTime->ContainerPtrToValuePtr<float>(this)) = 0.00f;
}


