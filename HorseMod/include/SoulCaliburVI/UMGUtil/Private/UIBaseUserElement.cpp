#include "UIBaseUserElement.h"

UUIBaseUserElement::UUIBaseUserElement() : UUserWidget(FObjectInitializer::Get()) {
    this->bUseAsUIElement = false;
    this->UINodeConfigurationScriptClass = NULL;
}



UUIEventListener* UUIBaseUserElement::AddEventListener(const FString& EventType) {
    return NULL;
}


