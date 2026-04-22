#include "MenuAnchor.h"

UMenuAnchor::UMenuAnchor() {
    this->MenuClass = NULL;
    this->Placement = MenuPlacement_ComboBox;
    this->ShouldDeferPaintingAfterWindowContent = true;
    this->UseApplicationMenuStack = true;
}

void UMenuAnchor::ToggleOpen(bool bFocusOnOpen) {
}

bool UMenuAnchor::ShouldOpenDueToClick() const {
    return false;
}

void UMenuAnchor::Open(bool bFocusMenu) {
}

bool UMenuAnchor::IsOpen() const {
    return false;
}

bool UMenuAnchor::HasOpenSubMenus() const {
    return false;
}

FVector2D UMenuAnchor::GetMenuPosition() const {
    return FVector2D{};
}

void UMenuAnchor::Close() {
}


