#pragma once
#include "CoreMinimal.h"
#include "PhysicsVolumeChangedDelegate.generated.h"

class APhysicsVolume;

UDELEGATE(BlueprintCallable) DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPhysicsVolumeChanged, APhysicsVolume*, NewVolume);

