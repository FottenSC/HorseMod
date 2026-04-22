#include "LuxRadialBlurCurve.h"

ULuxRadialBlurCurve::ULuxRadialBlurCurve() {
    this->CurveDataList.AddDefaulted(3);
}

bool ULuxRadialBlurCurve::GetParameterValue(ELuxRadialBlurCurveType CurveType, float InTime, float& ParamValue) const {
    return false;
}


