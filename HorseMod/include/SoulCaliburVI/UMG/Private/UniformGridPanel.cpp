#include "UniformGridPanel.h"
#include "ESlateVisibility.h"

UUniformGridPanel::UUniformGridPanel() {
    this->bIsVariable = false;
    this->Visiblity = ESlateVisibility::SelfHitTestInvisible;
    this->Visibility = ESlateVisibility::SelfHitTestInvisible;
    this->MinDesiredSlotWidth = 0.00f;
    this->MinDesiredSlotHeight = 0.00f;
}

void UUniformGridPanel::SetSlotPadding(FMargin InSlotPadding) {
}

void UUniformGridPanel::SetMinDesiredSlotWidth(float InMinDesiredSlotWidth) {
}

void UUniformGridPanel::SetMinDesiredSlotHeight(float InMinDesiredSlotHeight) {
}

UUniformGridSlot* UUniformGridPanel::AddChildToUniformGrid(UWidget* Content) {
    return NULL;
}


