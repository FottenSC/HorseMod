#include "TextLayoutWidget.h"

UTextLayoutWidget::UTextLayoutWidget() {
    this->Justification = ETextJustify::Left;
    this->AutoWrapText = false;
    this->WrapTextAt = 0.00f;
    this->WrappingPolicy = ETextWrappingPolicy::DefaultWrapping;
    this->LineHeightPercentage = 1.00f;
}


