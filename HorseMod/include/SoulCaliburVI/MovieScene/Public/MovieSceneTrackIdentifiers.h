#pragma once
#include "CoreMinimal.h"
#include "MovieSceneTrackIdentifier.h"
#include "MovieSceneTrackIdentifiers.generated.h"

USTRUCT(BlueprintType)
struct FMovieSceneTrackIdentifiers {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FMovieSceneTrackIdentifier> Data;
    
    MOVIESCENE_API FMovieSceneTrackIdentifiers();
};

