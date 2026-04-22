#include "UserInterfaceSettings.h"

UUserInterfaceSettings::UUserInterfaceSettings() {
    this->RenderFocusRule = ERenderFocusRule::NavigationOnly;
    this->ApplicationScale = 1.00f;
    this->UIScaleRule = EUIScalingRule::Vertical;
    this->bLoadWidgetsOnDedicatedServer = true;
    this->CustomScalingRuleClassInstance = NULL;
    this->CustomScalingRule = NULL;
}


