#pragma once
#include "CoreMinimal.h"
#include "MovieSceneSequenceHierarchyNode.h"
#include "MovieSceneSubSequenceData.h"
#include "MovieSceneSequenceHierarchy.generated.h"

USTRUCT(BlueprintType)
struct FMovieSceneSequenceHierarchy {
    GENERATED_BODY()
public:
private:
    UPROPERTY(EditAnywhere, meta=(AllowPrivateAccess=true))
    TMap<uint32, FMovieSceneSubSequenceData> SubSequences;
    
    UPROPERTY(EditAnywhere, meta=(AllowPrivateAccess=true))
    TMap<uint32, FMovieSceneSequenceHierarchyNode> Hierarchy;
    
public:
    MOVIESCENE_API FMovieSceneSequenceHierarchy();
};

