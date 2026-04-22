#include "HitResult.h"

FHitResult::FHitResult() {
    this->bBlockingHit = false;
    this->bStartPenetrating = false;
    this->Time = 0.00f;
    this->Distance = 0.00f;
    this->PenetrationDepth = 0.00f;
    this->Item = 0;
    this->FaceIndex = 0;
}

