#include "RBFParams.h"

FRBFParams::FRBFParams() {
    this->TargetDimensions = 0;
    this->Radius = 0.00f;
    this->Function = ERBFFunctionType::Gaussian;
    this->DistanceMethod = ERBFDistanceMethod::Euclidean;
    this->TwistAxis = BA_X;
    this->WeightThreshold = 0.00f;
}

