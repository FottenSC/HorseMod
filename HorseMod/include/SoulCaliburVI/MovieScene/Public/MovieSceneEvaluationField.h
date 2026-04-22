#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=FloatRange -FallbackName=FloatRange
#include "MovieSceneEvaluationGroup.h"
#include "MovieSceneEvaluationMetaData.h"
#include "MovieSceneEvaluationField.generated.h"

USTRUCT(BlueprintType)
struct FMovieSceneEvaluationField {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FFloatRange> rangeS;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FMovieSceneEvaluationGroup> Groups;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FMovieSceneEvaluationMetaData> MetaData;
    
    MOVIESCENE_API FMovieSceneEvaluationField();
};

