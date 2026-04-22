#pragma once
#include "CoreMinimal.h"
#include "ETransitionType.generated.h"

UENUM(BlueprintType)
enum ETransitionType {
    TT_None,
    TT_Paused,
    TT_Loading,
    TT_Saving,
    TT_Connecting,
    TT_Precaching,
    TT_WaitingToConnect,
};

