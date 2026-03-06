#pragma once
#include <AvUtils/avTypes.h>
typedef struct Shader_T* Shader;

typedef enum ShaderStageType{
    SHADER_STAGE_VERTEX = 1<<0,
    SHADER_STAGE_TESSELATION_CONTROL = 1<<1,
    SHADER_STAGE_TESSELATION_EVALUATION = 1<<2,
    SHADER_STAGE_GEOMETRY = 1<<3,
    SHADER_STAGE_FRAGMENT = 1<<4,
    SHADER_STAGE_COMPUTE = 1<<5,
} ShaderStageType;

typedef struct ShaderStageLoadInfo {
    const char* code;
    uint64 codeSize;
    ShaderStageType stage;
    const char* entry;
    const char* fileName;
} ShaderStageLoadInfo;

typedef enum ShaderOptimization {
    SHADER_OPTIMIZATION_O0,
    SHADER_OPTIMIZATION_O1,
    SHADER_OPTIMIZATION_Oz,
}ShaderOptimization;

typedef struct ShaderLoadInfo {
    uint32 shaderStageCount;
    ShaderStageLoadInfo* shaderStages;
    ShaderOptimization optimization;
} ShaderLoadInfo;


bool32 loadShader(ShaderLoadInfo info, Shader* shader);


//void shader
