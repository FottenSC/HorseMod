#include "AtomSound.h"
#include "AtomComponent.h"

AAtomSound::AAtomSound(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->bHidden = true;
    this->bCanBeDamaged = false;
    this->RootComponent = CreateDefaultSubobject<UAtomComponent>(TEXT("AtomComponent0"));
    this->AtomComponent = (UAtomComponent*)RootComponent;
}


