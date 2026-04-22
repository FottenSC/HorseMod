#include "DMSplineCollector.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=SplineComponent -FallbackName=SplineComponent

ADMSplineCollector::ADMSplineCollector(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->RootComponent = CreateDefaultSubobject<USplineComponent>(TEXT("Spline"));
    this->Spline = (USplineComponent*)RootComponent;
    this->Origin = NULL;
    this->bIgnoreLocZ = false;
    this->CalcOffset = 0.00f;
}

FTransform ADMSplineCollector::GetOptimalTransform(FVector InLoc) const {
    return FTransform{};
}


