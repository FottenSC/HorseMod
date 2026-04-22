#pragma once
#include "CoreMinimal.h"
#include "MovieSceneEvaluationField.h"
#include "MovieSceneEvaluationTrack.h"
#include "MovieSceneSequenceHierarchy.h"
#include "MovieSceneTemplateGenerationLedger.h"
#include "MovieSceneEvaluationTemplate.generated.h"

USTRUCT(BlueprintType)
struct FMovieSceneEvaluationTemplate {
    GENERATED_BODY()
public:
private:
    UPROPERTY(EditAnywhere, meta=(AllowPrivateAccess=true))
    TMap<uint32, FMovieSceneEvaluationTrack> Tracks;
    
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FMovieSceneEvaluationField EvaluationField;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FMovieSceneSequenceHierarchy Hierarchy;
    
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FMovieSceneTemplateGenerationLedger TemplateLedger;
    
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bHasLegacyTrackInstances: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bKeepStaleTracks: 1;
    
    MOVIESCENE_API FMovieSceneEvaluationTemplate();
};

