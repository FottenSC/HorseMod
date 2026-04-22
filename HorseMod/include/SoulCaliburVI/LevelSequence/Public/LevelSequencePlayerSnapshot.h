#pragma once
#include "CoreMinimal.h"
#include "LevelSequenceSnapshotSettings.h"
#include "LevelSequencePlayerSnapshot.generated.h"

class UCameraComponent;

USTRUCT(BlueprintType)
struct FLevelSequencePlayerSnapshot {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FText MasterName;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float MasterTime;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FText CurrentShotName;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float CurrentShotLocalTime;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    UCameraComponent* CameraComponent;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FLevelSequenceSnapshotSettings Settings;
    
    LEVELSEQUENCE_API FLevelSequencePlayerSnapshot();
};

