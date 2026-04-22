#include "MatineeTrackAtomCueId.h"
#include "MatineeTrackInstAtom.h"

UMatineeTrackAtomCueId::UMatineeTrackAtomCueId() {
    this->TrackInstClass = UMatineeTrackInstAtom::StaticClass();
    this->TrackTitle = TEXT("Atom Cue ID");
    this->CueSheet = NULL;
}


