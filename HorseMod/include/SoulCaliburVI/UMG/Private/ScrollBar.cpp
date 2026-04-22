#include "ScrollBar.h"

UScrollBar::UScrollBar() {
    this->bIsVariable = false;
    this->STYLE = NULL;
    this->bAlwaysShowScrollbar = true;
    this->Orientation = Orient_Vertical;
}

void UScrollBar::SetState(float InOffsetFraction, float InThumbSizeFraction) {
}


