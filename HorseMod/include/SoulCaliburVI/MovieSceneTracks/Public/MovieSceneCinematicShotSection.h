#pragma once
#include "CoreMinimal.h"
#include "MovieSceneSubSection.h"
#include "MovieSceneCinematicShotSection.generated.h"

UCLASS(Blueprintable)
class MOVIESCENETRACKS_API UMovieSceneCinematicShotSection : public UMovieSceneSubSection {
    GENERATED_BODY()
public:
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FText DisplayName;
    
public:
    UMovieSceneCinematicShotSection();

};

