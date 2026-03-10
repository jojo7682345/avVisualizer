#pragma once
#include <AvUtils/avTypes.h>
#include <vulkan/vulkan.h>
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
    SHADER_OPTIMIZATION_NONE,
    SHADER_OPTIMIZATION_SPEED,
    SHADER_OPTIMIZATION_SIZE,
}ShaderOptimization;

typedef enum ShaderLoadResult {
    SHADER_LOAD_SUCCESS = 0,
    SHADER_LOAD_ERROR_INVALID_OPTIMIZATION_LEVEL,
    SHADER_LOAD_ERROR_NO_SHADER_STAGES_SPECIFIED,
    SHADER_LOAD_ERROR_DUPLICATE_STAGES,
    SHADER_LOAD_ERROR_COMPUTE_AND_GRAPHICS_SHADERS_SPECIFIED,
    SHADER_LOAD_ERROR_COMPILATION_ERRORS,
    SHADER_LOAD_ERROR_NOT_IMPLEMENTED,
    SHADER_LOAD_ERROR_INVALID_SHADER_TYPE,
    SHADER_LOAD_ERROR_REQUIRED_STAGE_NOT_SPECIFIED,
    SHADER_LOAD_ERROR_OUTPUT_INVALID,
    SHADER_LOAD_ERROR_INPUT_INVALID,
    SHADER_LOAD_ERROR_INTERFACE_INVALID,
    SHADER_LOAD_ERROR_INCOMPATIBLE_PUSH_CONSTANTS,
    SHADER_LOAD_ERROR_PUSH_CONSTANTS_TOO_LARGE,
    SHADER_LOAD_ERROR_INVALID_DESCRIPTOR_SET,
    SHADER_LOAD_ERROR_DESCRIPTOR_TYPE_MISMATCH,
    SHADER_LOAD_ERROR_DESCRIPTOR_COUNT_MISMATCH,
    SHADER_LOAD_ERROR_DESCRIPTOR_STAGE_MISMATCH,
    SHADER_LOAD_ERROR_DESCRIPTOR_NOT_FOUND,
    SHADER_LOAD_ERROR_DESCRIPTOR_STAGE_CONFLICT,
    SHADER_LOAD_ERROR_PIPELINE_LAYOUT_CREATION_FAILED,
    SHADER_LOAD_ERROR_DESCRIPTOR_SET_LAYOUT_CREATION_FAILED,
    SHADER_LOAD_ERROR_SHADER_MODULE_CREATION_FAILED,
} ShaderLoadResult;

typedef struct ShaderLoadInfo {
    ShaderOptimization optimization;

    uint32 shaderStageCount;
    ShaderStageLoadInfo* shaderStages;
    
    uint32 vertexInputAttributeCount;
    VkVertexInputAttributeDescription* vertexInputAttributes;
    VkAttachmentDescription* attachements;
    VkSubpassDescription subpass;
    
    uint32 maxPushConstantsSize;

    VkPushConstantRange pushConstants;

    uint32 descriptorSetCount;
    VkDescriptorSetLayoutCreateInfo* descriptorSets;

} ShaderLoadInfo;

enum ShaderPipelineType {
	SHADER_PIPELINE_GRAPHICS,
	SHADER_PIPELINE_COMPUTE,
};

typedef struct Shader_T{
	enum ShaderPipelineType type;
	VkPipelineLayout pipelineLayout;
    uint32 setLayoutCount;
    VkDescriptorSetLayout* setLayouts;
	VkShaderModule* modules;
	uint32 shaderStageCount;
	VkPipelineShaderStageCreateInfo* stages;
} Shader_T;

ShaderLoadResult loadShader(VkDevice device, ShaderLoadInfo info, Shader* shader);
void unloadShader(VkDevice device, Shader shader);


//void shader
