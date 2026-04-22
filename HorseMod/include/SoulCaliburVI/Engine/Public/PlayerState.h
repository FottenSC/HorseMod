#pragma once
#include "CoreMinimal.h"
#include "Info.h"
#include "Templates/SubclassOf.h"
#include "UniqueNetIdRepl.h"
#include "PlayerState.generated.h"

class APlayerState;
class ULocalMessage;

UCLASS(Blueprintable, NotPlaceable)
class ENGINE_API APlayerState : public AInfo {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, ReplicatedUsing=OnRep_Score, meta=(AllowPrivateAccess=true))
    float Score;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Replicated, meta=(AllowPrivateAccess=true))
    uint8 Ping;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, ReplicatedUsing=OnRep_PlayerName, meta=(AllowPrivateAccess=true))
    FString PlayerName;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Replicated, meta=(AllowPrivateAccess=true))
    int32 PlayerId;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Replicated, meta=(AllowPrivateAccess=true))
    uint32 bIsSpectator: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Replicated, meta=(AllowPrivateAccess=true))
    uint32 bOnlySpectator: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Replicated, meta=(AllowPrivateAccess=true))
    uint32 bIsABot: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, ReplicatedUsing=OnRep_bIsInactive, meta=(AllowPrivateAccess=true))
    uint32 bIsInactive: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Replicated, meta=(AllowPrivateAccess=true))
    uint32 bFromPreviousLevel: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Replicated, meta=(AllowPrivateAccess=true))
    int32 StartTime;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TSubclassOf<ULocalMessage> EngineMessageClass;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FString SavedNetworkAddress;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, ReplicatedUsing=OnRep_UniqueId, meta=(AllowPrivateAccess=true))
    FUniqueNetIdRepl UniqueId;
    
    APlayerState(const FObjectInitializer& ObjectInitializer);

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void ReceiveOverrideWith(APlayerState* OldPlayerState);
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void ReceiveCopyProperties(APlayerState* NewPlayerState);
    
public:
    UFUNCTION(BlueprintCallable)
    void OnRep_UniqueId();
    
    UFUNCTION(BlueprintCallable)
    void OnRep_Score();
    
    UFUNCTION(BlueprintCallable)
    void OnRep_PlayerName();
    
    UFUNCTION(BlueprintCallable)
    void OnRep_bIsInactive();
    
};

