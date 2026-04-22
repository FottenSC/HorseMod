#pragma once
#include "CoreMinimal.h"
#include "EventPayload.h"
#include "MovieSceneEventSectionData.generated.h"

USTRUCT(BlueprintType)
struct FMovieSceneEventSectionData {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<float> KeyTimes;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FEventPayload> KeyValues;
    
    MOVIESCENETRACKS_API FMovieSceneEventSectionData();
};

