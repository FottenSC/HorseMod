#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Guid -FallbackName=Guid
#include "MovieSceneSequenceCachedSignature.generated.h"

class UMovieSceneSequence;

USTRUCT(BlueprintType)
struct FMovieSceneSequenceCachedSignature {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TWeakObjectPtr<UMovieSceneSequence> Sequence;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FGuid CachedSignature;
    
    MOVIESCENE_API FMovieSceneSequenceCachedSignature();
};

