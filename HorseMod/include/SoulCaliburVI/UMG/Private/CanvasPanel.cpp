#include "CanvasPanel.h"
#include "ESlateVisibility.h"

UCanvasPanel::UCanvasPanel() {
    this->bIsVariable = false;
    this->Visiblity = ESlateVisibility::SelfHitTestInvisible;
    this->Visibility = ESlateVisibility::SelfHitTestInvisible;
}

UCanvasPanelSlot* UCanvasPanel::AddChildToCanvas(UWidget* Content) {
    return NULL;
}


