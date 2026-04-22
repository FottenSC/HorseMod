#include "WrapBox.h"
#include "ESlateVisibility.h"

UWrapBox::UWrapBox() {
    this->bIsVariable = false;
    this->Visiblity = ESlateVisibility::SelfHitTestInvisible;
    this->Visibility = ESlateVisibility::SelfHitTestInvisible;
    this->WrapWidth = 500.00f;
    this->bExplicitWrapWidth = false;
}

void UWrapBox::SetInnerSlotPadding(FVector2D InPadding) {
}

UWrapBoxSlot* UWrapBox::AddChildWrapBox(UWidget* Content) {
    return NULL;
}


