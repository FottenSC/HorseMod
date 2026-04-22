#include "UIEventHub.h"

UUIEventHub::UUIEventHub() {
}

void UUIEventHub::DispatchTypedEvent(const FString& EventType, FUIDataObject EventParam) {
}

void UUIEventHub::DispatchEvent(const FUIDataObject& EventData) {
}

UUIEventListener* UUIEventHub::AddEventListener(const FString& EventType) {
    return NULL;
}


