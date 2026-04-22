#pragma once
#include "CoreMinimal.h"
#include "ETriangleSortOption.generated.h"

UENUM(BlueprintType)
enum ETriangleSortOption {
    TRISORT_None,
    TRISORT_CenterRadialDistance,
    TRISORT_Random,
    TRISORT_MergeContiguous,
    TRISORT_Custom,
    TRISORT_CustomLeftRight,
};

