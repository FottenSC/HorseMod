#include "GridPanel.h"
#include "ESlateVisibility.h"

UGridPanel::UGridPanel() {
    this->bIsVariable = false;
    this->Visiblity = ESlateVisibility::SelfHitTestInvisible;
    this->Visibility = ESlateVisibility::SelfHitTestInvisible;
}

UGridSlot* UGridPanel::AddChildToGrid(UWidget* Content) {
    return NULL;
}


