#include "KismetAnimationLibrary.h"

UKismetAnimationLibrary::UKismetAnimationLibrary() {
}

void UKismetAnimationLibrary::K2_TwoBoneIK(const FVector& RootPos, const FVector& JointPos, const FVector& EndPos, const FVector& JointTarget, const FVector& Effector, FVector& OutJointPos, FVector& OutEndPos, bool bAllowStretching, float StartStretchRatio, float MaxStretchScale) {
}

FTransform UKismetAnimationLibrary::K2_LookAt(const FTransform& CurrentTransform, const FVector& TargetPosition, FVector LookAtVector, bool bUseUpVector, FVector UpVector, float ClampConeInDegree) {
    return FTransform{};
}


