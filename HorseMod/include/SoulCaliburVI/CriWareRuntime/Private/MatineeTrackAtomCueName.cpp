#include "MatineeTrackAtomCueName.h"
#include "MatineeTrackInstAtom.h"

UMatineeTrackAtomCueName::UMatineeTrackAtomCueName() {
    this->TrackInstClass = UMatineeTrackInstAtom::StaticClass();
    this->TrackTitle = TEXT("Atom Cue Name");
    this->CueSheet = NULL;
}


