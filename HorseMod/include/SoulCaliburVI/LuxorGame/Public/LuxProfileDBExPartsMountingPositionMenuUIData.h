#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=UMGUtil -ObjectName=UIDataStructInterface -FallbackName=UIDataStructInterface
#include "ELuxOffsetParentBone.h"
#include "LuxProfileDBExPartsMountingPositionMenuUIData.generated.h"

USTRUCT(BlueprintType)
struct FLuxProfileDBExPartsMountingPositionMenuUIData : public FUIDataStructInterface {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    ELuxOffsetParentBone Key;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FString locLabelTextId;
    
    LUXORGAME_API FLuxProfileDBExPartsMountingPositionMenuUIData();
};

