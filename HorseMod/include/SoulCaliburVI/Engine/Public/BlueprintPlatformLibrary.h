#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=DateTime -FallbackName=DateTime
#include "BlueprintFunctionLibrary.h"
#include "BlueprintPlatformLibrary.generated.h"

UCLASS(Blueprintable)
class ENGINE_API UBlueprintPlatformLibrary : public UBlueprintFunctionLibrary {
    GENERATED_BODY()
public:
    UBlueprintPlatformLibrary();

    UFUNCTION(BlueprintCallable)
    static void ScheduleLocalNotificationFromNow(int32 inSecondsFromNow, const FText& Title, const FText& Body, const FText& Action, const FString& ActivationEvent);
    
    UFUNCTION(BlueprintCallable)
    static void ScheduleLocalNotificationBadgeFromNow(int32 inSecondsFromNow, const FString& ActivationEvent);
    
    UFUNCTION(BlueprintCallable)
    static void ScheduleLocalNotificationBadgeAtTime(const FDateTime& FireDateTime, bool LocalTime, const FString& ActivationEvent);
    
    UFUNCTION(BlueprintCallable)
    static void ScheduleLocalNotificationAtTime(const FDateTime& FireDateTime, bool LocalTime, const FText& Title, const FText& Body, const FText& Action, const FString& ActivationEvent);
    
    UFUNCTION(BlueprintCallable)
    static void GetLaunchNotification(bool& NotificationLaunchedApp, FString& ActivationEvent, int32& FireDate);
    
    UFUNCTION(BlueprintCallable)
    static void ClearAllLocalNotifications();
    
    UFUNCTION(BlueprintCallable)
    static void CancelLocalNotification(const FString& ActivationEvent);
    
};

