#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Guid -FallbackName=Guid
#include "MovieSceneTrackIdentifier.h"
#include "MovieSceneTrackIdentifiers.h"
#include "MovieSceneTemplateGenerationLedger.generated.h"

USTRUCT(BlueprintType)
struct FMovieSceneTemplateGenerationLedger {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FMovieSceneTrackIdentifier LastTrackIdentifier;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TMap<FMovieSceneTrackIdentifier, int32> TrackReferenceCounts;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TMap<FGuid, FMovieSceneTrackIdentifiers> TrackSignatureToTrackIdentifier;
    
    MOVIESCENE_API FMovieSceneTemplateGenerationLedger();
};

