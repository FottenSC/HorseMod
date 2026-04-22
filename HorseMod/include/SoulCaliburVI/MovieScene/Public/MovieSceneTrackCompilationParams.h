#pragma once
#include "CoreMinimal.h"
#include "MovieSceneTrackCompilationParams.generated.h"

USTRUCT(BlueprintType)
struct FMovieSceneTrackCompilationParams {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bForEditorPreview;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bDuringBlueprintCompile;
    
    MOVIESCENE_API FMovieSceneTrackCompilationParams();
};

