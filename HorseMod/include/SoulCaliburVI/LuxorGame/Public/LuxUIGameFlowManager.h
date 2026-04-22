#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=UMGUtil -ObjectName=UIGameFlowManager -FallbackName=UIGameFlowManager
#include "LuxSaveDataCheckParam.h"
#include "LuxUIGameFlowManager.generated.h"

class ULuxCeBankManager;
class ULuxGameSave;
class ULuxSigninManager;
class ULuxTournamentManager;
class ULuxUIAssetHub;
class ULuxUIAssetLoader;
class ULuxUIBattleLauncher;
class ULuxUIGameContent;
class ULuxUIGameStatusIconHandle;
class ULuxUIGameStatusManager;
class ULuxUINotification;
class ULuxUIShopFlow;
class ULuxUserPrivilegeManager;
class ULuxorSessionHub;

UCLASS(Blueprintable)
class LUXORGAME_API ULuxUIGameFlowManager : public UUIGameFlowManager {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    ULuxGameSave* GameSave;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    ULuxUIAssetHub* UIAssetHub;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    ULuxUIGameStatusManager* UIGameStatusManager;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    ULuxUIGameContent* UIGameContent;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    ULuxorSessionHub* SessionHub;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    ULuxSigninManager* SigninManager;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    ULuxUserPrivilegeManager* UserPrivilegeManager;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    ULuxUIBattleLauncher* BattleLauncher;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    ULuxUINotification* UINotification;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    ULuxUIShopFlow* UIShopFlow;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    ULuxTournamentManager* TournamentManager;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    ULuxCeBankManager* CeBankManager;
    
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    TArray<ULuxUIAssetLoader*> CachedUIAssetLoaders;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    ULuxUIGameStatusIconHandle* LoadIconHandle;
    
public:
    ULuxUIGameFlowManager();

    UFUNCTION(BlueprintCallable)
    void StartSaveDataCheckFlow(FLuxSaveDataCheckParam InParam);
    
protected:
    UFUNCTION(BlueprintCallable)
    void OnLuxGameUserSettingsResolutionChanged();
    
public:
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool IsStoryBattleMode() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool IsShinEdgeMasterBattleMode() const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool IsEnableTitleMovie() const;
    
    UFUNCTION(BlueprintCallable)
    void GameInitialize();
    
};

