#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
//CROSS-MODULE INCLUDE V2: -ModuleName=LuxorSessionUtil -ObjectName=LuxorBlueprintUserProfileData -FallbackName=LuxorBlueprintUserProfileData
#include "LuxMatchCharacterData.h"
#include "OnNgWordCheckCompletedDelegate.h"
#include "OnPermissionCheckCompletedDelegate.h"
#include "OnRecvBattleSyncStateDelegate.h"
#include "LuxMatchSettingSync.generated.h"

UCLASS(Blueprintable)
class ULuxMatchSettingSync : public UObject {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FOnRecvBattleSyncState OnRecvBattleSync;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FOnNgWordCheckCompleted OnNgWordCheckCompleted;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FOnPermissionCheckCompleted OnPermissionCheckCompleted;
    
    ULuxMatchSettingSync();

    UFUNCTION(BlueprintCallable)
    void SendStage(int32 InStageId, bool inIsRandom);
    
    UFUNCTION(BlueprintCallable)
    void SendProfileSet();
    
    UFUNCTION(BlueprintCallable)
    void SendGuestProfile();
    
    UFUNCTION(BlueprintCallable)
    void SendGuestCharacter(const FLuxMatchCharacterData& inPassiveCharaData);
    
    UFUNCTION(BlueprintCallable)
    void SendGuestAll(const FLuxMatchCharacterData& inPassiveCharaData);
    
    UFUNCTION(BlueprintCallable)
    void SendCharacterSet(const FLuxMatchCharacterData& inActiveCharaData, const FLuxMatchCharacterData& inPassiveCharaData);
    
    UFUNCTION(BlueprintCallable)
    void SendCharacterComplete();
    
    UFUNCTION(BlueprintCallable)
    void SendAllComplete();
    
    UFUNCTION(BlueprintCallable)
    void SendAll(const FLuxMatchCharacterData& inActiveCharaData, const FLuxMatchCharacterData& inPassiveCharaData, int32 InStageId);
    
    UFUNCTION(BlueprintCallable)
    void RequestVerifyString(const FString& inCharacterName);
    
    UFUNCTION(BlueprintCallable)
    void RequestStage();
    
    UFUNCTION(BlueprintCallable)
    void RequestProfile();
    
    UFUNCTION(BlueprintCallable)
    void RequestPermissionCheck(const FLuxorBlueprintUserProfileData& InProfileData);
    
    UFUNCTION(BlueprintCallable)
    void RequestCharacterSet();
    
    UFUNCTION(BlueprintCallable)
    void ReadyToConnect();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool IsRandomStage() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool IsConnected() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool IsCompleted() const;
    
    UFUNCTION(BlueprintCallable)
    void Initialize();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    int32 GetStageId() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    int32 GetRandomSeed() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FLuxorBlueprintUserProfileData GetProfileData(int32 PlayerIndex) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 GetMatchDataStyleId(const FLuxMatchCharacterData& inCharacterData);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    FLuxMatchCharacterData GetCharaData(int32 PlayerIndex) const;
    
    UFUNCTION(BlueprintCallable)
    void Finalize();
    
};

