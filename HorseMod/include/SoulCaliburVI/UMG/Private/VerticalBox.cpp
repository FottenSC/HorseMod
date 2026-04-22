#include "VerticalBox.h"
#include "ESlateVisibility.h"

UVerticalBox::UVerticalBox() {
    this->bIsVariable = false;
    this->Visiblity = ESlateVisibility::SelfHitTestInvisible;
    this->Visibility = ESlateVisibility::SelfHitTestInvisible;
}

UVerticalBoxSlot* UVerticalBox::AddChildToVerticalBox(UWidget* Content) {
    return NULL;
}


