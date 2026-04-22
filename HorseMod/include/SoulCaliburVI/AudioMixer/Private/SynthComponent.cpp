#include "SynthComponent.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=AudioComponent -FallbackName=AudioComponent

USynthComponent::USynthComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->bAutoActivate = true;
    this->bUseAttachParentBound = true;
    this->bAutoDestroy = false;
    this->bStopWhenOwnerDestroyed = true;
    this->bAllowSpatialization = false;
    this->bOverrideAttenuation = false;
    this->AttenuationSettings = NULL;
    this->ConcurrencySettings = NULL;
    this->SourceEffectChain = NULL;
    this->DefaultMasterReverbSendAmount = 0.00f;
    this->SoundSubmix = NULL;
    this->Synth = NULL;
    this->AudioComponent = CreateDefaultSubobject<UAudioComponent>(TEXT("AudioComponent"));
}

void USynthComponent::Stop() {
}

void USynthComponent::Start() {
}

void USynthComponent::SetSubmixSend(USoundSubmix* Submix, float SendLevel) {
}

bool USynthComponent::IsPlaying() const {
    return false;
}


