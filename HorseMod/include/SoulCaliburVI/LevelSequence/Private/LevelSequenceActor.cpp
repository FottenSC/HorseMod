#include "LevelSequenceActor.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=SceneComponent -FallbackName=SceneComponent
//CROSS-MODULE INCLUDE V2: -ModuleName=MovieScene -ObjectName=MovieSceneBindingOverrides -FallbackName=MovieSceneBindingOverrides
#include "LevelSequenceBurnInOptions.h"

ALevelSequenceActor::ALevelSequenceActor(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SceneComp"));
    this->bAutoPlay = false;
    this->SequencePlayer = NULL;
    this->BurnInOptions = CreateDefaultSubobject<ULevelSequenceBurnInOptions>(TEXT("BurnInOptions"));
    this->BindingOverrides = CreateDefaultSubobject<UMovieSceneBindingOverrides>(TEXT("BindingOverrides"));
    this->BurnInInstance = NULL;
}

void ALevelSequenceActor::SetSequence(ULevelSequence* InSequence) {
}

void ALevelSequenceActor::SetEventReceivers(TArray<AActor*> AdditionalReceivers) {
}

void ALevelSequenceActor::SetBinding(FMovieSceneObjectBindingID Binding, const TArray<AActor*>& Actors, bool bAllowBindingsFromAsset) {
}

void ALevelSequenceActor::ResetBindings() {
}

void ALevelSequenceActor::ResetBinding(FMovieSceneObjectBindingID Binding) {
}

void ALevelSequenceActor::RemoveBinding(FMovieSceneObjectBindingID Binding, AActor* Actor) {
}

ULevelSequence* ALevelSequenceActor::GetSequence(bool bLoad, bool bInitializePlayer) const {
    return NULL;
}

void ALevelSequenceActor::AddBinding(FMovieSceneObjectBindingID Binding, AActor* Actor, bool bAllowBindingsFromAsset) {
}


