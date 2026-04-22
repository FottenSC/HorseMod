#include "RichCurve.h"

FRichCurve::FRichCurve() {
    this->PreInfinityExtrap = RCCE_Cycle;
    this->PostInfinityExtrap = RCCE_Cycle;
    this->DefaultValue = 0.00f;
}

