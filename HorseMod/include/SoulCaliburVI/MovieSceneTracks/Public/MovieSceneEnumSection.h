#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=IntegralCurve -FallbackName=IntegralCurve
//CROSS-MODULE INCLUDE V2: -ModuleName=MovieScene -ObjectName=MovieSceneSection -FallbackName=MovieSceneSection
#include "MovieSceneEnumSection.generated.h"

UCLASS(Blueprintable, MinimalAPI)
class UMovieSceneEnumSection : public UMovieSceneSection {
    GENERATED_BODY()
public:
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FIntegralCurve EnumCurve;
    
public:
    UMovieSceneEnumSection();

};

