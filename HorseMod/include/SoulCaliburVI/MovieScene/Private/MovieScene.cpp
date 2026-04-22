#include "MovieScene.h"

UMovieScene::UMovieScene() {
    this->CameraCutTrack = NULL;
    this->bForceFixedFrameIntervalPlayback = false;
    this->FixedFrameInterval = 0.00f;
    this->InTime = 340282346638528859811704183484516925440.00f;
    this->OutTime = -340282346638528859811704183484516925440.00f;
    this->StartTime = 340282346638528859811704183484516925440.00f;
    this->EndTime = -340282346638528859811704183484516925440.00f;
}


