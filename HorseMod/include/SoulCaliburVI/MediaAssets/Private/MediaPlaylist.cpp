#include "MediaPlaylist.h"

UMediaPlaylist::UMediaPlaylist() {
    this->Loop = false;
}

bool UMediaPlaylist::Replace(int32 index, UMediaSource* Replacement) {
    return false;
}

bool UMediaPlaylist::RemoveAt(int32 index) {
    return false;
}

bool UMediaPlaylist::Remove(UMediaSource* MediaSource) {
    return false;
}

int32 UMediaPlaylist::Num() {
    return 0;
}

void UMediaPlaylist::Insert(UMediaSource* MediaSource, int32 index) {
}

UMediaSource* UMediaPlaylist::GetRandom(int32& OutIndex) {
    return NULL;
}

UMediaSource* UMediaPlaylist::GetPrevious(int32& InOutIndex) {
    return NULL;
}

UMediaSource* UMediaPlaylist::GetNext(int32& InOutIndex) {
    return NULL;
}

UMediaSource* UMediaPlaylist::Get(int32 index) {
    return NULL;
}

bool UMediaPlaylist::AddUrl(const FString& URL) {
    return false;
}

bool UMediaPlaylist::AddFile(const FString& FilePath) {
    return false;
}

bool UMediaPlaylist::Add(UMediaSource* MediaSource) {
    return false;
}


