#include "MaterialQualityOverrides.h"

FMaterialQualityOverrides::FMaterialQualityOverrides() {
    this->bEnableOverride = false;
    this->bForceFullyRough = false;
    this->bForceNonMetal = false;
    this->bForceDisableLMDirectionality = false;
    this->bForceLQReflections = false;
    this->MobileCSMQuality = EMobileCSMQuality::NoFiltering;
}

