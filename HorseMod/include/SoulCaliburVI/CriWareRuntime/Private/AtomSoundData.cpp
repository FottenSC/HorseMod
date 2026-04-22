#include "AtomSoundData.h"

AAtomSoundData::AAtomSoundData(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->bHidden = true;
    this->bCanBeDamaged = false;
    this->CueSheet = NULL;
}


