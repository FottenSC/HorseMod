#include "LuxDesaturationCurve.h"

ULuxDesaturationCurve::ULuxDesaturationCurve() {
    this->CurveDataList.AddDefaulted(2);
}

bool ULuxDesaturationCurve::GetParameterValue(ELuxDesaturationCurveType CurveType, float InTime, float& ParamValue) const {
    return false;
}


