#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
#include "ELuxFightStyle.h"
#include "ELuxUIAstralClearLevel.h"
#include "ELuxUIMissionDifficulty.h"
#include "ELuxUIPlayerSoulCondition.h"
#include "ELuxUIPlayerSoulLevel.h"
#include "LuxUIExpStatus.h"
#include "LuxUIPlayerProfile.h"
#include "LuxUIPlayerProfileUtil.generated.h"

class ULuxCreationProfile;

UCLASS(Blueprintable)
class LUXORGAME_API ULuxUIPlayerProfileUtil : public UObject {
    GENERATED_BODY()
public:
    ULuxUIPlayerProfileUtil();

    UFUNCTION(BlueprintCallable)
    static void SetStyleStatus(UPARAM(Ref) FLuxUIPlayerProfile& InPlayerProfile, const FString& InStyleName, FLuxUIExpStatus InStyleStatus);
    
    UFUNCTION(BlueprintCallable)
    static void SetEquipWeaponIndex(UPARAM(Ref) FLuxUIPlayerProfile& InPlayerProfile, int32 WeaponIndex);
    
    UFUNCTION(BlueprintCallable)
    static void GetStyleStatus(const FLuxUIPlayerProfile& InPlayerProfile, const FString& InStyleName, FLuxUIExpStatus& StyleStatus);
    
    UFUNCTION(BlueprintCallable)
    static int32 GetStyleInt(const FLuxUIPlayerProfile& InPlayerProfile, ULuxCreationProfile* inProfile);
    
    UFUNCTION(BlueprintCallable)
    static int32 GetSoulPoint(const FLuxUIPlayerProfile& InPlayerProfile, ELuxUIPlayerSoulCondition InSoulCondition);
    
    UFUNCTION(BlueprintCallable)
    static ELuxUIPlayerSoulLevel GetSoulLevel(const FLuxUIPlayerProfile& InPlayerProfile, ELuxUIPlayerSoulCondition InSoulCondition);
    
    UFUNCTION(BlueprintCallable)
    static int32 GetMoneyLevel(const FLuxUIPlayerProfile& InPlayerProfile);
    
    UFUNCTION(BlueprintCallable)
    static ELuxUIMissionDifficulty GetMissionDifficulty(const FLuxUIPlayerProfile& InPlayerProfile);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 GetLibraMax();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static int32 GetGoldMax();
    
    UFUNCTION(BlueprintCallable)
    static int32 GetEquipWeaponIndex(const FLuxUIPlayerProfile& InPlayerProfile);
    
    UFUNCTION(BlueprintCallable)
    static ELuxUIAstralClearLevel GetAstralMissionClearLevel(const int32& InClearCount);
    
    UFUNCTION(BlueprintCallable)
    static bool CheckStyleLevelOver(const FLuxUIPlayerProfile& InPlayerProfile, ELuxFightStyle InStyle, int32 InLevel);
    
    UFUNCTION(BlueprintCallable)
    static void AddSoulPoint(UPARAM(Ref) FLuxUIPlayerProfile& InPlayerProfile, ELuxUIPlayerSoulCondition InSoulCondition, int32 InSoulPoint);
    
};

