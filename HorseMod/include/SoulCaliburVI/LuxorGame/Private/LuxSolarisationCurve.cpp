#include "LuxSolarisationCurve.h"

ULuxSolarisationCurve::ULuxSolarisationCurve() {
    this->CurveDataList.AddDefaulted(5);
}

bool ULuxSolarisationCurve::GetParameterValue(ELuxSolarisationCurveType CurveType, float InTime, float& ParamValue) const {
    return false;
}


