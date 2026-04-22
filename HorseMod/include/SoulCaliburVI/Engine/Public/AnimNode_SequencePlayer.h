#pragma once
#include "CoreMinimal.h"
#include "AnimNode_AssetPlayerBase.h"
#include "AnimNode_SequencePlayer.generated.h"

class UAnimSequenceBase;

USTRUCT(BlueprintType)
struct ENGINE_API FAnimNode_SequencePlayer : public FAnimNode_AssetPlayerBase {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    UAnimSequenceBase* Sequence;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bLoopAnimation;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float PlayRate;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float StartPosition;
    
    FAnimNode_SequencePlayer();
};

