#include "HorizontalBox.h"
#include "ESlateVisibility.h"

UHorizontalBox::UHorizontalBox() {
    this->bIsVariable = false;
    this->Visiblity = ESlateVisibility::SelfHitTestInvisible;
    this->Visibility = ESlateVisibility::SelfHitTestInvisible;
}

UHorizontalBoxSlot* UHorizontalBox::AddChildToHorizontalBox(UWidget* Content) {
    return NULL;
}


