#include "EditableTextBox.h"

UEditableTextBox::UEditableTextBox() {
    this->STYLE = NULL;
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

void UEditableTextBox::SetText(FText InText) {
}

void UEditableTextBox::SetIsReadOnly(bool bReadOnly) {
}

void UEditableTextBox::SetHintText(FText InText) {
}

void UEditableTextBox::SetError(FText InError) {
}

bool UEditableTextBox::HasError() const {
    return false;
}

FText UEditableTextBox::GetText() const {
    return FText::GetEmpty();
}

void UEditableTextBox::ClearError() {
}


