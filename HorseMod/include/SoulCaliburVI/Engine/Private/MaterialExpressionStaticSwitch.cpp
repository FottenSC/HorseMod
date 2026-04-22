#include "MaterialExpressionStaticSwitch.h"

UMaterialExpressionStaticSwitch::UMaterialExpressionStaticSwitch() {
    this->Outputs.AddDefaulted(1);
    this->DefaultValue = false;
}


