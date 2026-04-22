#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=MovieScene -ObjectName=MovieSceneSequence -FallbackName=MovieSceneSequence
#include "OnWidgetAnimationPlaybackStatusChangedDelegate.h"
#include "WidgetAnimationBinding.h"
#include "WidgetAnimation.generated.h"

class UMovieScene;

UCLASS(Blueprintable, DefaultToInstanced, MinimalAPI)
class UWidgetAnimation : public UMovieSceneSequence {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FOnWidgetAnimationPlaybackStatusChanged OnAnimationStarted;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FOnWidgetAnimationPlaybackStatusChanged OnAnimationFinished;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    UMovieScene* MovieScene;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FWidgetAnimationBinding> AnimationBindings;
    
    UWidgetAnimation();

    UFUNCTION(BlueprintCallable, BlueprintPure)
    float GetStartTime() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    float GetEndTime() const;
    
};

