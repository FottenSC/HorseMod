#pragma once
#include "CoreMinimal.h"
#include "EMediaWebcamCaptureDeviceFilter.generated.h"

UENUM(BlueprintType)
enum class EMediaWebcamCaptureDeviceFilter : uint8 {
    DepthSensor = 1,
    Front,
    Rear = 4,
    Unknown = 8,
};

