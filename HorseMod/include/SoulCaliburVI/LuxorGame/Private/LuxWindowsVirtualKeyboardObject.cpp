#include "LuxWindowsVirtualKeyboardObject.h"

ULuxWindowsVirtualKeyboardObject::ULuxWindowsVirtualKeyboardObject() {
    this->KeyboardWidget = NULL;
    this->InputText = NULL;
}

void ULuxWindowsVirtualKeyboardObject::OnTextChangedHandler(const FText& Text) {
}

void ULuxWindowsVirtualKeyboardObject::OnCompletionHandler(const FText& Text, TEnumAsByte<ETextCommit::Type> CommitMethod) {
}


