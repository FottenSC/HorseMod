#include "EditableText.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=SlateCore -ObjectName=EWidgetClipping -FallbackName=EWidgetClipping

UEditableText::UEditableText() {
    this->Clipping = EWidgetClipping::ClipToBounds;
    this->STYLE = NULL;
    this->BackgroundImageSelected = NULL;
    this->BackgroundImageComposing = NULL;
    this->CaretImage = NULL;
    this->IsReadOnly = false;
    this->IsPassword = false;
    this->MinimumDesiredWidth = 0.00f;
    this->IsCaretMovedWhenGainFocus = true;
    this->SelectAllTextWhenFocused = false;
    this->RevertTextOnEscape = false;
    this->ClearKeyboardFocusOnCommit = true;
    this->SelectAllTextOnCommit = false;
    this->AllowContextMenu = true;
    this->KeyboardType = EVirtualKeyboardType::Default;
}

void UEditableText::SetText(FText InText) {
}

void UEditableText::SetIsReadOnly(bool InbIsReadyOnly) {
}

void UEditableText::SetIsPassword(bool InbIsPassword) {
}

void UEditableText::SetHintText(FText InHintText) {
}

FText UEditableText::GetText() const {
    return FText::GetEmpty();
}


