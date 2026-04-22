#pragma once
#include "CoreMinimal.h"
#include "LevelSequenceSnapshotSettings.generated.h"

USTRUCT(BlueprintType)
struct FLevelSequenceSnapshotSettings {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint8 ZeroPadAmount;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float FrameRate;
    
    LEVELSEQUENCE_API FLevelSequenceSnapshotSettings();
};

