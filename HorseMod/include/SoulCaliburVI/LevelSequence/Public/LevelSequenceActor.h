#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=StringAssetReference -FallbackName=StringAssetReference
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=Actor -FallbackName=Actor
//CROSS-MODULE INCLUDE V2: -ModuleName=MovieScene -ObjectName=MovieSceneBindingOwnerInterface -FallbackName=MovieSceneBindingOwnerInterface
//CROSS-MODULE INCLUDE V2: -ModuleName=MovieScene -ObjectName=MovieSceneObjectBindingID -FallbackName=MovieSceneObjectBindingID
//CROSS-MODULE INCLUDE V2: -ModuleName=MovieScene -ObjectName=MovieSceneSequencePlaybackSettings -FallbackName=MovieSceneSequencePlaybackSettings
#include "LevelSequenceActor.generated.h"

class ULevelSequence;
class ULevelSequenceBurnIn;
class ULevelSequenceBurnInOptions;
class ULevelSequencePlayer;
class UMovieSceneBindingOverrides;

UCLASS(Blueprintable)
class LEVELSEQUENCE_API ALevelSequenceActor : public AActor, public IMovieSceneBindingOwnerInterface {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bAutoPlay;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FMovieSceneSequencePlaybackSettings PlaybackSettings;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    ULevelSequencePlayer* SequencePlayer;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FStringAssetReference LevelSequence;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<AActor*> AdditionalEventReceivers;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    ULevelSequenceBurnInOptions* BurnInOptions;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    UMovieSceneBindingOverrides* BindingOverrides;
    
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    ULevelSequenceBurnIn* BurnInInstance;
    
public:
    ALevelSequenceActor(const FObjectInitializer& ObjectInitializer);

    UFUNCTION(BlueprintCallable)
    void SetSequence(ULevelSequence* InSequence);
    
    UFUNCTION(BlueprintCallable)
    void SetEventReceivers(TArray<AActor*> AdditionalReceivers);
    
    UFUNCTION(BlueprintCallable)
    void SetBinding(FMovieSceneObjectBindingID Binding, const TArray<AActor*>& Actors, bool bAllowBindingsFromAsset);
    
    UFUNCTION(BlueprintCallable)
    void ResetBindings();
    
    UFUNCTION(BlueprintCallable)
    void ResetBinding(FMovieSceneObjectBindingID Binding);
    
    UFUNCTION(BlueprintCallable)
    void RemoveBinding(FMovieSceneObjectBindingID Binding, AActor* Actor);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    ULevelSequence* GetSequence(bool bLoad, bool bInitializePlayer) const;
    
    UFUNCTION(BlueprintCallable)
    void AddBinding(FMovieSceneObjectBindingID Binding, AActor* Actor, bool bAllowBindingsFromAsset);
    

    // Fix for true pure virtual functions not being implemented
};

