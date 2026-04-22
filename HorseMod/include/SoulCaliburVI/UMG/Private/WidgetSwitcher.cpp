#include "WidgetSwitcher.h"
#include "ESlateVisibility.h"

UWidgetSwitcher::UWidgetSwitcher() {
    this->Visiblity = ESlateVisibility::SelfHitTestInvisible;
    this->Visibility = ESlateVisibility::SelfHitTestInvisible;
    this->ActiveWidgetIndex = 0;
}

void UWidgetSwitcher::SetActiveWidgetIndex(int32 index) {
}

void UWidgetSwitcher::SetActiveWidget(UWidget* Widget) {
}

UWidget* UWidgetSwitcher::GetWidgetAtIndex(int32 index) const {
    return NULL;
}

int32 UWidgetSwitcher::GetNumWidgets() const {
    return 0;
}

int32 UWidgetSwitcher::GetActiveWidgetIndex() const {
    return 0;
}


