#include "ScrollBox.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=SlateCore -ObjectName=EWidgetClipping -FallbackName=EWidgetClipping

UScrollBox::UScrollBox() {
    this->bIsVariable = false;
    this->Clipping = EWidgetClipping::ClipToBounds;
    this->STYLE = NULL;
    this->BarStyle = NULL;
    this->Orientation = Orient_Vertical;
    this->ScrollBarVisibility = ESlateVisibility::Visible;
    this->ConsumeMouseWheel = EConsumeMouseWheel::WhenScrollingPossible;
    this->AlwaysShowScrollbar = false;
    this->AllowOverscroll = true;
    this->NavigationDestination = EDescendantScrollDestination::IntoView;
    this->NavigationScrollPadding = 0.00f;
}

void UScrollBox::SetScrollOffset(float NewScrollOffset) {
}

void UScrollBox::SetScrollBarVisibility(ESlateVisibility NewScrollBarVisibility) {
}

void UScrollBox::SetScrollbarThickness(const FVector2D& NewScrollbarThickness) {
}

void UScrollBox::SetOrientation(TEnumAsByte<EOrientation> NewOrientation) {
}

void UScrollBox::SetAlwaysShowScrollbar(bool NewAlwaysShowScrollbar) {
}

void UScrollBox::SetAllowOverscroll(bool NewAllowOverscroll) {
}

void UScrollBox::ScrollWidgetIntoView(UWidget* WidgetToFind, bool AnimateScroll, EDescendantScrollDestination ScrollDesintion) {
}

void UScrollBox::ScrollToStart() {
}

void UScrollBox::ScrollToEnd() {
}

float UScrollBox::GetScrollOffset() const {
    return 0.0f;
}

float UScrollBox::GetEndScrollOffset() const {
    return 0.0f;
}


