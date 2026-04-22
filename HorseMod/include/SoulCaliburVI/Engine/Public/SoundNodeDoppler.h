#pragma once
#include "CoreMinimal.h"
#include "SoundNode.h"
#include "SoundNodeDoppler.generated.h"

UCLASS(Blueprintable, EditInlineNew)
class USoundNodeDoppler : public USoundNode {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float DopplerIntensity;
    
    USoundNodeDoppler();

};

