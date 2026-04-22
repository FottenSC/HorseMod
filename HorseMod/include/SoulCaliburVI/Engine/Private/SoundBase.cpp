#include "SoundBase.h"

USoundBase::USoundBase() {
    this->bDebug = false;
    this->bOverrideConcurrency = false;
    this->bIgnoreFocus = false;
    this->SoundConcurrencySettings = NULL;
    this->MaxConcurrentResolutionRule = EMaxConcurrentResolutionRule::PreventNew;
    this->MaxConcurrentPlayCount = 16;
    this->Duration = 0.00f;
    this->AttenuationSettings = NULL;
    this->Priority = 1.00f;
    this->SoundSubmixObject = NULL;
    this->DefaultMasterReverbSendAmount = 0.20f;
    this->SourceEffectChain = NULL;
}


