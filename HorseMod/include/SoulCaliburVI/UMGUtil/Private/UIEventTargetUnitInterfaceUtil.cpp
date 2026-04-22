#include "UIEventTargetUnitInterfaceUtil.h"

UUIEventTargetUnitInterfaceUtil::UUIEventTargetUnitInterfaceUtil() {
}

UUIEventListener* UUIEventTargetUnitInterfaceUtil::ListenEvent(UObject* Target, const FString& Type) {
    return NULL;
}

bool UUIEventTargetUnitInterfaceUtil::HasEventTargetUintInterface(UObject* Target) {
    return false;
}

void UUIEventTargetUnitInterfaceUtil::DispatchEvent(UObject* Target, const FString& Type, FUIDataObject Param) {
}


