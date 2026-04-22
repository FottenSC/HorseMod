#pragma once
#include "CoreMinimal.h"
#include "EApplicationState.h"
#include "EScreenOrientation.h"
#include "GameInstance.h"
#include "PlatformGameInstance.generated.h"

UCLASS(Blueprintable, NonTransient)
class ENGINE_API UPlatformGameInstance : public UGameInstance {
    GENERATED_BODY()
public:
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPlatformScreenOrientationChangedDelegate, TEnumAsByte<EScreenOrientation::Type>, inScreenOrientation);
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPlatformRegisteredForUserNotificationsDelegate, int32, inInt);
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPlatformRegisteredForRemoteNotificationsDelegate, const TArray<uint8>&, inArray);
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPlatformReceivedRemoteNotificationDelegate, const FString&, inString, TEnumAsByte<EApplicationState::Type>, inAppState);
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FPlatformReceivedLocalNotificationDelegate, const FString&, inString, int32, inInt, TEnumAsByte<EApplicationState::Type>, inAppState);
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPlatformFailedToRegisterForRemoteNotificationsDelegate, const FString&, inString);
    DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPlatformDelegate);
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FPlatformDelegate ApplicationWillDeactivateDelegate;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FPlatformDelegate ApplicationHasReactivatedDelegate;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FPlatformDelegate ApplicationWillEnterBackgroundDelegate;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FPlatformDelegate ApplicationHasEnteredForegroundDelegate;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FPlatformDelegate ApplicationWillTerminateDelegate;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FPlatformRegisteredForRemoteNotificationsDelegate ApplicationRegisteredForRemoteNotificationsDelegate;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FPlatformRegisteredForUserNotificationsDelegate ApplicationRegisteredForUserNotificationsDelegate;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FPlatformFailedToRegisterForRemoteNotificationsDelegate ApplicationFailedToRegisterForRemoteNotificationsDelegate;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FPlatformReceivedRemoteNotificationDelegate ApplicationReceivedRemoteNotificationDelegate;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FPlatformReceivedLocalNotificationDelegate ApplicationReceivedLocalNotificationDelegate;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FPlatformScreenOrientationChangedDelegate ApplicationReceivedScreenOrientationChangedNotificationDelegate;
    
    UPlatformGameInstance();

};

