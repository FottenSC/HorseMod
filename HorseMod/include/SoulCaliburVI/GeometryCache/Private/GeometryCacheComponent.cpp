#include "GeometryCacheComponent.h"

UGeometryCacheComponent::UGeometryCacheComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->GeometryCache = NULL;
    this->bRunning = false;
    this->bLooping = true;
    this->StartTimeOffset = 0.00f;
    this->PlaybackSpeed = 1.00f;
    this->NumTracks = 0;
    this->ElapsedTime = 0.00f;
}

void UGeometryCacheComponent::Stop() {
}

void UGeometryCacheComponent::SetStartTimeOffset(const float NewStartTimeOffset) {
}

void UGeometryCacheComponent::SetPlaybackSpeed(const float NewPlaybackSpeed) {
}

void UGeometryCacheComponent::SetLooping(const bool bNewLooping) {
}

bool UGeometryCacheComponent::SetGeometryCache(UGeometryCache* NewGeomCache) {
    return false;
}

void UGeometryCacheComponent::PlayReversedFromEnd() {
}

void UGeometryCacheComponent::PlayReversed() {
}

void UGeometryCacheComponent::PlayFromStart() {
}

void UGeometryCacheComponent::Play() {
}

void UGeometryCacheComponent::Pause() {
}

bool UGeometryCacheComponent::IsPlayingReversed() const {
    return false;
}

bool UGeometryCacheComponent::IsPlaying() const {
    return false;
}

bool UGeometryCacheComponent::IsLooping() const {
    return false;
}

float UGeometryCacheComponent::GetStartTimeOffset() const {
    return 0.0f;
}

float UGeometryCacheComponent::GetPlaybackSpeed() const {
    return 0.0f;
}


