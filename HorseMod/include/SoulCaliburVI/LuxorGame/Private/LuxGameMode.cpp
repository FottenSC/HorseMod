#include "LuxGameMode.h"
#include "LuxHUD.h"
#include "LuxPawn.h"
#include "LuxPlayerController.h"

ALuxGameMode::ALuxGameMode(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->PlayerControllerClass = ALuxPlayerController::StaticClass();
    this->HUDClass = ALuxHUD::StaticClass();
    this->DefaultPawnClass = ALuxPawn::StaticClass();
}


