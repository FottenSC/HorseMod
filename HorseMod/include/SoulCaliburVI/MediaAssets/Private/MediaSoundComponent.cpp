#include "MediaSoundComponent.h"

UMediaSoundComponent::UMediaSoundComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->Channels = EMediaSoundChannels::Stereo;
    this->MediaPlayer = NULL;
}


