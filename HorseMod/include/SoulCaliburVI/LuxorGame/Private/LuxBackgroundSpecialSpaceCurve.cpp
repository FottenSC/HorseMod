#include "LuxBackgroundSpecialSpaceCurve.h"

ULuxBackgroundSpecialSpaceCurve::ULuxBackgroundSpecialSpaceCurve() {
    this->CurveDataList.AddDefaulted(2);
}

bool ULuxBackgroundSpecialSpaceCurve::GetParameterValue(ELuxBackgroundSpecialSpaceCurveType CurveType, float InTime, float& ParamValue) const {
    return false;
}


