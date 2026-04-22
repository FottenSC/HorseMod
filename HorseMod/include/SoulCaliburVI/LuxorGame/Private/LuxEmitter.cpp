#include "LuxEmitter.h"
#include "LuxParticleSystemComponent.h"

ALuxEmitter::ALuxEmitter(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->RootComponent = CreateDefaultSubobject<ULuxParticleSystemComponent>(TEXT("LuxParticleSystemComponent0"));
    this->LuxParticleSystemComponent = (ULuxParticleSystemComponent*)RootComponent;
}


