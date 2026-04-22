#include "TextBlock.h"

UTextBlock::UTextBlock() {
    this->bIsVariable = false;
    this->MinDesiredWidth = 0.00f;
    this->bWrapWithInvalidationPanel = false;
}

void UTextBlock::SetText(FText InText) {
}

void UTextBlock::SetShadowOffset(FVector2D InShadowOffset) {
}

void UTextBlock::SetShadowColorAndOpacity(FLinearColor InShadowColorAndOpacity) {
}

void UTextBlock::SetOpacity(float InOpacity) {
}

void UTextBlock::SetMinDesiredWidth(float InMinDesiredWidth) {
}

void UTextBlock::SetJustification(TEnumAsByte<ETextJustify::Type> InJustification) {
}

void UTextBlock::SetFont(FSlateFontInfo InFontInfo) {
}

void UTextBlock::SetColorAndOpacity(FSlateColor InColorAndOpacity) {
}

FText UTextBlock::GetText() const {
    return FText::GetEmpty();
}


