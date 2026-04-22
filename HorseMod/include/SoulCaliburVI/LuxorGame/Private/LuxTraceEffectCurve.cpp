#include "LuxTraceEffectCurve.h"

ULuxTraceEffectCurve::ULuxTraceEffectCurve() {
    this->CurveData.AddDefaulted(7);
}

float ULuxTraceEffectCurve::GetParamValue(ELuxTraceEffectCurveType CurveType, float InTime) const {
    return 0.0f;
}


