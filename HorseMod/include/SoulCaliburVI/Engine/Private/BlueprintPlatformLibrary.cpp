#include "BlueprintPlatformLibrary.h"

UBlueprintPlatformLibrary::UBlueprintPlatformLibrary() {
}

void UBlueprintPlatformLibrary::ScheduleLocalNotificationFromNow(int32 inSecondsFromNow, const FText& Title, const FText& Body, const FText& Action, const FString& ActivationEvent) {
}

void UBlueprintPlatformLibrary::ScheduleLocalNotificationBadgeFromNow(int32 inSecondsFromNow, const FString& ActivationEvent) {
}

void UBlueprintPlatformLibrary::ScheduleLocalNotificationBadgeAtTime(const FDateTime& FireDateTime, bool LocalTime, const FString& ActivationEvent) {
}

void UBlueprintPlatformLibrary::ScheduleLocalNotificationAtTime(const FDateTime& FireDateTime, bool LocalTime, const FText& Title, const FText& Body, const FText& Action, const FString& ActivationEvent) {
}

void UBlueprintPlatformLibrary::GetLaunchNotification(bool& NotificationLaunchedApp, FString& ActivationEvent, int32& FireDate) {
}

void UBlueprintPlatformLibrary::ClearAllLocalNotifications() {
}

void UBlueprintPlatformLibrary::CancelLocalNotification(const FString& ActivationEvent) {
}


