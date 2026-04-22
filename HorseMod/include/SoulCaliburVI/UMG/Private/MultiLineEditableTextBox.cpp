#include "MultiLineEditableTextBox.h"

UMultiLineEditableTextBox::UMultiLineEditableTextBox() {
    this->AutoWrapText = true;
    this->bIsReadOnly = false;
    this->AllowContextMenu = true;
    this->STYLE = NULL;
}

void UMultiLineEditableTextBox::SetText(FText InText) {
}

void UMultiLineEditableTextBox::SetIsReadOnly(bool bReadOnly) {
}

void UMultiLineEditableTextBox::SetError(FText InError) {
}

FText UMultiLineEditableTextBox::GetText() const {
    return FText::GetEmpty();
}


