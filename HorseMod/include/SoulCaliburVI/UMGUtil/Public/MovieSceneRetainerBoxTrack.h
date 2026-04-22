#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=MovieSceneTracks -ObjectName=MovieSceneComponentMaterialTrack -FallbackName=MovieSceneComponentMaterialTrack
#include "MovieSceneRetainerBoxTrack.generated.h"

UCLASS(Blueprintable, MinimalAPI)
class UMovieSceneRetainerBoxTrack : public UMovieSceneComponentMaterialTrack {
    GENERATED_BODY()
public:
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FName TrackName;
    
public:
    UMovieSceneRetainerBoxTrack();

};

