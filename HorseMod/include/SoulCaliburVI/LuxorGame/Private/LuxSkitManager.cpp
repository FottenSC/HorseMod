#include "LuxSkitManager.h"

ULuxSkitManager::ULuxSkitManager() {
    this->SkitWidget = NULL;
    this->DemoResource = NULL;
}

bool ULuxSkitManager::TogglePause() {
    return false;
}

void ULuxSkitManager::StopSkit() {
}

void ULuxSkitManager::StartSkit(ULuxSkitWidget* inWidget, ULuxStoryDemoResourceDataAsset* inDemoResource) {
}

void ULuxSkitManager::Skip() {
}

void ULuxSkitManager::SetReadyToFinish(bool inReadyToFinish) {
}

void ULuxSkitManager::SetDecidedBranchIndex(int32 index) {
}

void ULuxSkitManager::SetAutoSkipFrame(int32 inFrame) {
}

void ULuxSkitManager::NextMessage() {
}

bool ULuxSkitManager::IsPausing() {
    return false;
}

void ULuxSkitManager::Initialize() {
}

void ULuxSkitManager::Finalize() {
}

bool ULuxSkitManager::CanSkip() const {
    return false;
}

bool ULuxSkitManager::CanShowLog() const {
    return false;
}


