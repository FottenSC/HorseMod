#pragma once
#include "CoreMinimal.h"
#include "BTCompositeNode.h"
#include "BTComposite_WeightedRandom.generated.h"

UCLASS(Blueprintable)
class AIMODULE_API UBTComposite_WeightedRandom : public UBTCompositeNode {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float LeftChildSelectingRate;
    
    UBTComposite_WeightedRandom();

};

