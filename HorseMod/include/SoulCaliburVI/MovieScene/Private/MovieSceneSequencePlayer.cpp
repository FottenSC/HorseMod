#include "MovieSceneSequencePlayer.h"

UMovieSceneSequencePlayer::UMovieSceneSequencePlayer() {
    this->Status = EMovieScenePlayerStatus::Stopped;
    this->bReversePlayback = false;
    this->bPendingFirstUpdate = false;
    this->Sequence = NULL;
    this->TimeCursorPosition = 0.00f;
    this->StartTime = 0.00f;
    this->EndTime = 0.00f;
    this->CurrentNumLoops = 0;
}

void UMovieSceneSequencePlayer::Stop() {
}

void UMovieSceneSequencePlayer::StartPlayingNextTick() {
}

void UMovieSceneSequencePlayer::SetPlayRate(float PlayRate) {
}

void UMovieSceneSequencePlayer::SetPlaybackRange(const float NewStartTime, const float NewEndTime) {
}

void UMovieSceneSequencePlayer::SetPlaybackPosition(float NewPlaybackPosition) {
}

void UMovieSceneSequencePlayer::Scrub() {
}

void UMovieSceneSequencePlayer::PlayReverse() {
}

void UMovieSceneSequencePlayer::PlayLooping(int32 NumLoops) {
}

void UMovieSceneSequencePlayer::Play() {
}

void UMovieSceneSequencePlayer::Pause() {
}

void UMovieSceneSequencePlayer::JumpToPosition(float NewPlaybackPosition) {
}

bool UMovieSceneSequencePlayer::IsPlaying() const {
    return false;
}

void UMovieSceneSequencePlayer::GoToEndAndStop() {
}

float UMovieSceneSequencePlayer::GetPlayRate() const {
    return 0.0f;
}

float UMovieSceneSequencePlayer::GetPlaybackStart() const {
    return 0.0f;
}

float UMovieSceneSequencePlayer::GetPlaybackPosition() const {
    return 0.0f;
}

float UMovieSceneSequencePlayer::GetPlaybackEnd() const {
    return 0.0f;
}

float UMovieSceneSequencePlayer::GetLength() const {
    return 0.0f;
}

TArray<UObject*> UMovieSceneSequencePlayer::GetBoundObjects(FMovieSceneObjectBindingID ObjectBinding) {
    return TArray<UObject*>();
}

void UMovieSceneSequencePlayer::ChangePlaybackDirection() {
}


