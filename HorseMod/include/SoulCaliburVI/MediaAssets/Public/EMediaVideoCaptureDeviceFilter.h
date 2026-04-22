#pragma once
#include "CoreMinimal.h"
#include "EMediaVideoCaptureDeviceFilter.generated.h"

UENUM(BlueprintType)
enum class EMediaVideoCaptureDeviceFilter : uint8 {
    Card = 1,
    Software,
    Unknown = 4,
    Webcam = 8,
};

