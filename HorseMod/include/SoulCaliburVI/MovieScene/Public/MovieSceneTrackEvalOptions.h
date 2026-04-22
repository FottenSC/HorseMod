#pragma once
#include "CoreMinimal.h"
#include "MovieSceneTrackEvalOptions.generated.h"

USTRUCT(BlueprintType)
struct FMovieSceneTrackEvalOptions {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bCanEvaluateNearestSection: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bEvalNearestSection: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bEvaluateInPreroll: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bEvaluateInPostroll: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bEvaluateNearestSection: 1;
    
    MOVIESCENE_API FMovieSceneTrackEvalOptions();
};

