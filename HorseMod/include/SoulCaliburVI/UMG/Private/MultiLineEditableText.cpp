#include "MultiLineEditableText.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=SlateCore -ObjectName=EWidgetClipping -FallbackName=EWidgetClipping

UMultiLineEditableText::UMultiLineEditableText() {
    this->Clipping = EWidgetClipping::ClipToBounds;
    this->AutoWrapText = true;
    this->bIsReadOnly = false;
    this->AllowContextMenu = true;
}

void UMultiLineEditableText::SetText(FText InText) {
}

void UMultiLineEditableText::SetIsReadOnly(bool bReadOnly) {
}

FText UMultiLineEditableText::GetText() const {
    return FText::GetEmpty();
}


