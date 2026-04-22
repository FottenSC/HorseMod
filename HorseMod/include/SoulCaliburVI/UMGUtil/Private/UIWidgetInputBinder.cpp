#include "UIWidgetInputBinder.h"

UUIWidgetInputBinder::UUIWidgetInputBinder() {
    this->bIgnoreDefaultPrevented = true;
}

void UUIWidgetInputBinder::OnHandleInput_Implementation(UBaseUserWidget* Widget, EUIInputType InputType, int32 ControllerId, EUIInputKey Key, bool IsDefaultPrevented, bool& bPreventDefault) {
}


