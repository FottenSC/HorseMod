#include "LuxBattleTrainingManager.h"

ALuxBattleTrainingManager::ALuxBattleTrainingManager(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->mode = ELuxBattleTrainingMode::Free;
}

void ALuxBattleTrainingManager::StoreTrainingUIMenu() {
}

void ALuxBattleTrainingManager::SetMode(ELuxBattleTrainingMode inMode) {
}

void ALuxBattleTrainingManager::LoadTrainingUIMenu() {
}

ALuxBattleTrainingReplayPlayer* ALuxBattleTrainingManager::GetTrainingReplayPlayer() {
    return NULL;
}

ALuxBattlePositionResetter* ALuxBattleTrainingManager::GetPositionResetter() {
    return NULL;
}

ALuxBattleMoveCommandPlayer* ALuxBattleTrainingManager::GetMoveCommandPlayer() {
    return NULL;
}

ELuxBattleTrainingMode ALuxBattleTrainingManager::GetMode() const {
    return ELuxBattleTrainingMode::Free;
}

ALuxBattleKeyRecorder* ALuxBattleTrainingManager::GetKeyRecorder() {
    return NULL;
}

ALuxBattleGaugeTypeChanger* ALuxBattleTrainingManager::GetGaugeTypeChanger() {
    return NULL;
}

ALuxBattleDummyCustomizer* ALuxBattleTrainingManager::GetDummyCustomizer() {
    return NULL;
}

ALuxBattleAICustomizer* ALuxBattleTrainingManager::GetAICustomizer() {
    return NULL;
}


