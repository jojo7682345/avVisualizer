#include <entry.h>
#include <ecs/ecs.h>

extern bool8 renderFrameCpp(struct Application*);

extern bool8 initializeCpp(struct Application*);

Scene scene = {0};

EntityType ENTITY_TYPE_FOO = INVALID_ENTITY_TYPE;
EntityType ENTITY_TYPE_BAR = INVALID_ENTITY_TYPE;

ComponentID COMPONENT_TYPE_FOO = INVALID_COMPONENT_ID;
ComponentID COMPONENT_TYPE_BAR = 5;
ComponentID COMPONENT_TYPE_BAZ = INVALID_COMPONENT_ID;

typedef struct FooComponent {
    uint32 a;
} FooComponent;

typedef struct BarComponent {
    uint32 a;
    uint32 b;
} BarComponent;

void fooConstructor(Scene scene, Entity entity, void* component, uint32 size, va_list args){
    ((FooComponent*)component)->a = va_arg(args, uint32);
    avLog(AV_DEBUG, "Initialized foo component of entity %x, with %u", entity, ((FooComponent*)component)->a);
}
void fooDestructor(Scene scene, Entity entity, void* component, uint32 size){
    avLog(AV_DEBUG, "Uninitialized foo component of entity %x, with %u", entity, ((FooComponent*)component)->a);
}

void barConstructor(Scene scene, Entity entity, void* component, uint32 size, va_list args){
    ((BarComponent*)component)->a = va_arg(args, uint32);
    ((BarComponent*)component)->b = va_arg(args, uint32);
    avLog(AV_DEBUG, "Initialized foo component of entity %x, with %u %u", entity, ((BarComponent*)component)->a, ((BarComponent*)component)->b);
}
void barDestructor(Scene scene, Entity entity, void* component, uint32 size){
    avLog(AV_DEBUG, "Uninitialized foo component of entity %x, with %u %u", entity, ((BarComponent*)component)->a, ((BarComponent*)component)->b);
}

void testFunc(Scene scene, const Entity entity, const uint32 componentCount, const ComponentID* componentIndex, const Component* component, const uint32* componentSizes){
    for(uint32 i = 0; i < componentCount; i++){
        if(componentIndex[i]==COMPONENT_TYPE_FOO){
            ((FooComponent*)component[i])->a += 1;
            avLog(AV_DEBUG, "Performed for entity FOO %x", entity);
            continue;
        }
        if(componentIndex[i]==COMPONENT_TYPE_BAR){
            ((BarComponent*)component[i])->b += 1;
            ((BarComponent*)component[i])->a += 2;
            avLog(AV_DEBUG, "Performed for entity BAR %x", entity);
            continue;
        }

        
    }
}

bool8 initialize(struct Application* app){
    initializeCpp(app);

    scene = sceneCreate();
    registerComponent(&COMPONENT_TYPE_FOO, sizeof(FooComponent), fooConstructor, fooDestructor);
    registerComponent(&COMPONENT_TYPE_BAR, sizeof(BarComponent), barConstructor, barDestructor);
    registerComponent(&COMPONENT_TYPE_BAZ, 0, NULL, NULL);

    ComponentID barComponents[] = { COMPONENT_TYPE_FOO, COMPONENT_TYPE_BAR, COMPONENT_TYPE_BAZ };

    ENTITY_TYPE_FOO = entityTypeCreate(scene, 1, &COMPONENT_TYPE_FOO);
    ENTITY_TYPE_BAR = entityTypeCreate(scene, 2, barComponents);
    Entity foo = entityCreate(scene, ENTITY_TYPE_FOO);
    entityAddComponent(scene, foo, COMPONENT_TYPE_FOO, 15);

    Entity bar[15];
    for(uint32 i = 0; i < 15; i++){
        bar[i] = entityCreate(scene, ENTITY_TYPE_BAR);
        entityAddComponent(scene, bar[i], COMPONENT_TYPE_FOO, 20);
        entityAddComponent(scene, bar[i], COMPONENT_TYPE_BAR, 1, 2);
    }

    entityRemoveComponent(scene, bar[2], COMPONENT_TYPE_FOO);

    scenePerformForEntitiesMasked(scene, componentMaskMake(COMPONENT_TYPE_FOO, COMPONENT_TYPE_BAR), testFunc);

    return true;
}

bool8 renderFrame(struct Application* app){
    renderFrameCpp(app);

    return true;
}

bool8 update(struct Application* app_inst){
    return true;
}

bool8 prepareFrame(struct Application* app_inst){
    return true;
}

void onResize(struct Application* app_inst, uint32 width, uint32 height){

}

void shutdown(struct Application* app_inst){
    sceneDestroy(scene);
}

const char* disabledLogCategories[] = {
	//"avixel",
	//"avixel_core",
	//"avixel_renderer"
};
uint disabledLogCategoryCount = sizeof(disabledLogCategories) / sizeof(const char*);

AvResult disabledLogMessages[] = {
	//AV_DEBUG_CREATE,
	//AV_DEBUG_DESTROY,
};
uint disabledLogMessageCount = sizeof(disabledLogMessages) / sizeof(AvResult);

// Define the function to create a game
bool8 create_application(Application* out_application) {

    AvLogSettings logSettings = avLogSettingsDefault;
	logSettings.printSuccess = false;
	logSettings.printCode = false;
	logSettings.printAssert = false;
	logSettings.printType = true;
	logSettings.printFunc = false;
	logSettings.printError = true;
	logSettings.printCategory = false;
	logSettings.colors = true;
	logSettings.disabledCategories = disabledLogCategories;
	logSettings.disabledCategoryCount = disabledLogCategoryCount;
	logSettings.disabledMessages = disabledLogMessages;
	logSettings.disabledMessageCount = disabledLogMessageCount;
	logSettings.validationLevel = AV_VALIDATION_LEVEL_WARNINGS_AND_ERRORS;
	logSettings.level = AV_LOG_LEVEL_ALL;
    out_application->logSettings = &logSettings;

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