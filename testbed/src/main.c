#include <entry.h>

bool8 initialize(struct Application* app_inst){

}

bool8 update(struct Application* app_inst){

}

bool8 prepareFrame(struct Application* app_inst){

}

bool8 renderFrame(struct Application* app_inst){

}

void onResize(struct Application* app_inst, uint32 width, uint32 height){

}

void shutdown(struct Application* app_inst){

}

// Define the function to create a game
bool8 create_application(Application* out_application) {
    // Application configuration.
    out_application->appConfig.startPosX = 100;
    out_application->appConfig.startPosY = 100;
    out_application->appConfig.startWidth = 1280;
    out_application->appConfig.startHeight = 720;
    out_application->appConfig.name = "AvVisualizer TestBed!";

    out_application->engineState = 0;
    out_application->state = 0;

    out_application->initialize = initialize;
    out_application->update = update;
    out_application->prepareFrame = prepareFrame;
    out_application->renderFrame = renderFrame;
    out_application->on_resize = onResize;
    out_application->shutdown = shutdown;


    return true;
}

bool8 initialize_application(Application* app) {

    

    return true;
}