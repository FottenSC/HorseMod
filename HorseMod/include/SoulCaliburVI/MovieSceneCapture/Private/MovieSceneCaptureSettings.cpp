#include "MovieSceneCaptureSettings.h"

FMovieSceneCaptureSettings::FMovieSceneCaptureSettings() {
    this->bCreateTemporaryCopiesOfLevels = false;
    this->GameModeOverride = NULL;
    this->bOverwriteExisting = false;
    this->bUseRelativeFrameNumbers = false;
    this->HandleFrames = 0;
    this->ZeroPadFrameNumbers = 0;
    this->FrameRate = 0;
    this->bEnableTextureStreaming = false;
    this->bCinematicEngineScalability = false;
    this->bCinematicMode = false;
    this->bAllowMovement = false;
    this->bAllowTurning = false;
    this->bShowPlayer = false;
    this->bShowHUD = false;
}

