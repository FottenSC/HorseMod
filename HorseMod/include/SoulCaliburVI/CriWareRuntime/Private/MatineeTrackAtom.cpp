#include "MatineeTrackAtom.h"
#include "MatineeTrackInstAtom.h"

UMatineeTrackAtom::UMatineeTrackAtom() {
    this->TrackInstClass = UMatineeTrackInstAtom::StaticClass();
    this->TrackTitle = TEXT("Atom Cue");
}


