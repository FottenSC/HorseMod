#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Vector2D -FallbackName=Vector2D
#include "LuxGameSaveObjectHelper.h"
#include "LuxShinEdgeMasterEncountData.generated.h"

USTRUCT(BlueprintType)
struct FLuxShinEdgeMasterEncountData : public FLuxGameSaveObjectHelper {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FVector2D Position;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FString MissionID;
    
    LUXORGAME_API FLuxShinEdgeMasterEncountData();
};

