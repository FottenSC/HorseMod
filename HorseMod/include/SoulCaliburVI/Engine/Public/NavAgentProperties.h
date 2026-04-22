#pragma once
#include "CoreMinimal.h"
#include "MovementProperties.h"
#include "Templates/SubclassOf.h"
#include "NavAgentProperties.generated.h"

class ANavigationData;

USTRUCT(BlueprintType)
struct ENGINE_API FNavAgentProperties : public FMovementProperties {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float AgentRadius;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float AgentHeight;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float AgentStepHeight;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float NavWalkingSearchHeightScale;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TSubclassOf<ANavigationData> PreferredNavData;
    
    FNavAgentProperties();
};

