#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
#include "EMovieScenePlayerStatus.h"
#include "MovieSceneObjectBindingID.h"
#include "MovieSceneSequencePlaybackSettings.h"
#include "OnMovieSceneSequencePlayerEventDelegate.h"
#include "MovieSceneSequencePlayer.generated.h"

class UMovieSceneSequence;

UCLASS(Abstract, Blueprintable)
class MOVIESCENE_API UMovieSceneSequencePlayer : public UObject {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FOnMovieSceneSequencePlayerEvent OnPlay;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FOnMovieSceneSequencePlayerEvent OnStop;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FOnMovieSceneSequencePlayerEvent OnPause;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FOnMovieSceneSequencePlayerEvent OnFinished;
    
protected:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TEnumAsByte<EMovieScenePlayerStatus::Type> Status;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bReversePlayback: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bPendingFirstUpdate: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    UMovieSceneSequence* Sequence;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float TimeCursorPosition;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float StartTime;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float EndTime;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    int32 CurrentNumLoops;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FMovieSceneSequencePlaybackSettings PlaybackSettings;
    
public:
    UMovieSceneSequencePlayer();

    UFUNCTION(BlueprintCallable)
    void Stop();
    
    UFUNCTION(BlueprintCallable)
    void StartPlayingNextTick();
    
    UFUNCTION(BlueprintCallable)
    void SetPlayRate(float PlayRate);
    
    UFUNCTION(BlueprintCallable)
    void SetPlaybackRange(const float NewStartTime, const float NewEndTime);
    
    UFUNCTION(BlueprintCallable)
    void SetPlaybackPosition(float NewPlaybackPosition);
    
    UFUNCTION(BlueprintCallable)
    void Scrub();
    
    UFUNCTION(BlueprintCallable)
    void PlayReverse();
    
    UFUNCTION(BlueprintCallable)
    void PlayLooping(int32 NumLoops);
    
    UFUNCTION(BlueprintCallable)
    void Play();
    
    UFUNCTION(BlueprintCallable)
    void Pause();
    
    UFUNCTION(BlueprintCallable)
    void JumpToPosition(float NewPlaybackPosition);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool IsPlaying() const;
    
    UFUNCTION(BlueprintCallable)
    void GoToEndAndStop();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    float GetPlayRate() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    float GetPlaybackStart() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    float GetPlaybackPosition() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    float GetPlaybackEnd() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    float GetLength() const;
    
    UFUNCTION(BlueprintCallable)
    TArray<UObject*> GetBoundObjects(FMovieSceneObjectBindingID ObjectBinding);
    
    UFUNCTION(BlueprintCallable)
    void ChangePlaybackDirection();
    
};

