#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <shaderc/shaderc.h>

typedef shaderc_shader_kind ShaderType;


#ifdef _WIN32
#define PATH_SEP '\\'
#else
#define PATH_SEP '/'
#endif

// bin/shaderExport 
// -MD -c 
// -o build/tmp/shaderExport/basic_pipeline/basic_shader.h 
// shader/src/basic_pipeline/basic_shader.frag 
// shaders/src/basic_pipeline/basic_shader.vert

// basic_shader.h
// header guard
// basic_shader_vert_data
// basic_shader_vert_size
// basic_shader_frag_data
// basic_shader_frag_size
// end header guard

typedef struct ShaderFileData {

	const char* vertFile;
	const char* fragFile;
	const char* compFile;

	const char* shaderName;

	const char* outputFile;
}ShaderFileData;

typedef struct ShaderSource {
	const char* source;
	uint32_t size;
} ShaderSource;

typedef struct ShaderBufferData {
	ShaderSource vertSource;
	ShaderSource fragSource;
	ShaderSource compSource;
}ShaderBufferData;

typedef struct ShaderBinary {
	unsigned char* data;
	size_t size;
} ShaderBinary;

typedef struct ShaderBinaryData {
	ShaderBinary vertBinary;
	ShaderBinary fragBinary;
	ShaderBinary compBinary;
}ShaderBinaryData;


char* shift_args(int* argc, char*** argv) {
	if (*argc <= 0) {
		printf("Not enough arguments specified\n");
		exit(-1);
	}
	char* result = **argv;
	*argc -= 1;
	*argv += 1;
	return result;
}

unsigned long long hash(unsigned char* str) {
	unsigned long long hash = 5381;
	int c;

	while (c = *str++)
		hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

	return hash;
}

int ends_with(const char* str, const char* postFix) {

	int postLength = strlen(postFix);
	int strLength = strlen(str);

	if (strLength < postLength) {
		return 0;
	}

	int offset = strLength - postLength;

	char* post = (char*)str + offset;

	for (int i = 0; i < postLength; i++) {
		if (post[i] != postFix[i]) {
			return 0;
		}
	}

	return 1;
}

shaderc_shader_kind getShaderType(const char* file) {
	shaderc_shader_kind type;
	if (ends_with(file, ".vert")) {
		type = shaderc_glsl_vertex_shader;
	} else if (ends_with(file, ".frag")) {
		type = shaderc_glsl_fragment_shader;
	} else if (ends_with(file, ".comp")) {
		type = shaderc_glsl_compute_shader;
	} else {
		printf("unsuported shader file\n");
		exit(-1);
	}
	return type;
}

int checkShaderFileDataValid(ShaderFileData data) {
	if (!data.outputFile) {
		return 0;
	}

	return (data.vertFile || data.fragFile || data.compFile);
}

int loadShaderFile(const char* shaderFile, ShaderSource* source) {

	FILE* sourceFile = fopen(shaderFile, "rb");
	if (!sourceFile) {
		printf("Could not open file %s\n", shaderFile);
		return -1;
	}
	fseek(sourceFile, 0, SEEK_END);
	size_t size = ftell(sourceFile);
	fseek(sourceFile, 0, SEEK_SET);

	unsigned char* buffer = (unsigned char*)malloc(size + 1);
	if (!buffer) {
		printf("out of mem\n");
		return -1;
	}
	memset(buffer, 0, size + 1);
	if (fread(buffer, sizeof(char), size, sourceFile) != size) {
		printf("unable to read entire file\n");
		return -1;
	}

	if (fclose(sourceFile)) {
		printf("failed to close file\n");
		return -1;
	}

	source->source = buffer;
	source->size = size;

	return 0;
}


void getShaderName(const char* file, int* shaderNameLength, char** shaderName) {

	{
		char* end = (char*)file + strlen(file);
		int isName = 0;
		while (end != file) {
			if (isName) {
				(*shaderNameLength)++;
			}
			if (end[0] == PATH_SEP
			#ifdef _WIN32
				|| end[0] == '/'
			#endif
			) {
				*shaderName = end + 1;
				break;
			}
			
			if (end[0] == '.') {
				isName = 1;
			}
			end--;
			*shaderName = end;
		}
		if (!isName) {
			printf("invalid file\n");
			exit(-1);
			return;
		}
		(*shaderNameLength)--;
	}

	char* nameBuffer = (char*)malloc(*shaderNameLength + 1);
	if (!nameBuffer) {
		printf("out of mem\n");
		exit(-1);
		return;
	}
	memset(nameBuffer, 0, *shaderNameLength + 1);
	memcpy(nameBuffer, *shaderName, *shaderNameLength);
	*shaderName = nameBuffer;
}


void printShader(FILE* file, const char* shaderName, ShaderBinary* binary, ShaderType type) {

	const char* typeStr;
	switch (type) {
	case shaderc_glsl_vertex_shader:
		typeStr = "vert";
		break;
	case shaderc_glsl_fragment_shader:
		typeStr = "frag";
		break;
	case shaderc_glsl_compute_shader:
		typeStr = "comp";
		break;
	}

	fprintf(file, "const char %s_%s_data[] = {", shaderName, typeStr);
	for (int i = 0; i < binary->size; i++) {
		if (i % 16 == 0) {
			fprintf(file, "\n\t");
		} else if (i % 8 == 0) {
			fprintf(file, " ");
		}
		fprintf(file, "0x%02x, ", binary->data[i]);
	}
	fprintf(file, "\n};\n\n");
	fprintf(file, "const unsigned long long int %s_%s_size = sizeof(%s_%s_data)/sizeof(char);\n", shaderName, typeStr, shaderName, typeStr);

}

const char* getPath(const char* file) {
	int length = strlen(file);
	int pathLength = 0;
	for (int i = length; length >= 0; i--) {
		if (file[i] == PATH_SEP) {
			pathLength = i;
			break;
		}
	}

	char* buffer = (char*)malloc(pathLength + 1);
	if (buffer == NULL) {
		printf("out of mem\n");
		exit(-1);
		return NULL;
	}
	memcpy(buffer, file, pathLength);
	buffer[pathLength] = '\0';
	return buffer;
}

// __/etc.file


int processShader(const char* file, ShaderBinary* binary, ShaderType type, shaderc_compiler_t compiler, shaderc_compile_options_t options) {
	if (file) {
		ShaderSource source = { 0 };
		if (loadShaderFile(file, &source) != 0) {
			printf("failed to load shader\n");
			exit(-1);
			return 0;
		}
		shaderc_compilation_result_t result = shaderc_compile_into_spv(
			compiler,
			source.source,
			source.size,
			type,
			file,
			"main",
			options
		);
		size_t numWarnings = shaderc_result_get_num_warnings(result);
		size_t numErrors = shaderc_result_get_num_errors(result);
		shaderc_compilation_status status = shaderc_result_get_compilation_status(result);
	
		if (status != shaderc_compilation_status_success) {
			const char* errorMsg = shaderc_result_get_error_message(result);
			fprintf(stderr, "compilation error:\n%s\n\ncompilation resulted in %i warnings and %i errors\n",errorMsg, numWarnings, numErrors);
			shaderc_result_release(result);
			return 0;
		}

		size_t size = shaderc_result_get_length(result);
		binary->data = (char*)malloc(size);
		if (!binary->data) {
			fprintf(stderr, "out of mem\n");
			exit(-1);
			shaderc_result_release(result);
			return 0;
		}
		memcpy(binary->data, shaderc_result_get_bytes(result), size);
		binary->size = size;

		shaderc_result_release(result);

		return 1;
	} else {
		return 0;
	}
}

typedef struct DarrayHeader {
	size_t capacity;
	size_t count;
} DarrayHeader;

#define ARRAY_INIT_CAPACITY 1
#define darrayPush(array, x)\
do{																									\
	if(array == NULL) {																				\
		DarrayHeader* header = malloc(sizeof(*(array))*ARRAY_INIT_CAPACITY + sizeof(DarrayHeader));	\
		header->count = 0;																			\
		header->capacity = ARRAY_INIT_CAPACITY;														\
		array = (void*)(header + 1);																\
	}																								\
	DarrayHeader* header = (DarrayHeader*)(array) - 1;												\
	if(header->count >= header->capacity){															\
		header->capacity *= 2;																		\
		header = realloc(header, sizeof(*(array))*header->capacity + sizeof(DarrayHeader));			\
		array = (void*)(header + 1);																\
	}																								\
	(array)[header->count++] = (x);																	\
} while(0)																							\

#define darrayLength(array) ((DarrayHeader*)(array) -1)->count
#define darrayFree(array) free((DarrayHeader*)(array) - 1)

int main(int argC, char* argV[]) {

	const char* program = shift_args(&argC, &argV);

	ShaderFileData fileData = { 0 };

	int generateDepFile = 0;
	int optimisationLevel = 0;
	char** shaderFiles = NULL;
	while (1) {
		char* argument = shift_args(&argC, &argV);
		if (strcmp(argument, "-c") == 0) {
			continue;
		}
		if (strcmp(argument, "-MD") == 0) {
			generateDepFile = 1;
			continue;
		}
		if (strlen(argument) > 2 && argument[0] == '-' && argument[1] == 'O' ) {
			sscanf(argument, "-O%1i", &optimisationLevel);
			continue;
		}
		if (strcmp(argument, "-o") == 0) {
			fileData.outputFile = shift_args(&argC, &argV);
			continue;
		}

		darrayPush(shaderFiles, argument);

		if (argC == 0) {
			break;
		}
	}

	if (shaderFiles==NULL) {
		fprintf(stderr, "no shder files specified\n");
		return -1;
	}

	

	for(int i = 0; i < darrayLength(shaderFiles); i++){
		


		ShaderType type = getShaderType(shaderFiles[i]);
		char* buff;
		switch (type) {
		case shaderc_glsl_vertex_shader:
			if (fileData.vertFile) {
				fprintf(stderr, "vertex shader already specified!\n");
				return -1;
			}
			buff = (char*)malloc(2048);
			if (buff == 0) {
				fprintf(stderr, "out of mem\n");
				return -1;
			}
			memcpy(buff, shaderFiles[i], 2048);
			fileData.vertFile = buff;
			break;
		case shaderc_glsl_fragment_shader:
			if (fileData.fragFile) {
				fprintf(stderr, "fragment shader already specified!\n");
				return -1;
			}
			buff = (char*)malloc(2048);
			if (buff == 0) {
				fprintf(stderr, "out of mem\n");
				return -1;
			}
			memcpy(buff, shaderFiles[i], 2048);
			fileData.fragFile = buff;
			break;
		case shaderc_glsl_compute_shader:
			if (fileData.compFile) {
				fprintf(stderr, "compute shader already specified!\n");
				return -1;
			}
			buff = (char*)malloc(2048);
			if (buff == 0) {
				fprintf(stderr, "out of mem\n");
				return -1;
			}
			memcpy(buff, shaderFiles[i], 2048);
			fileData.compFile = buff;
			break;
		}
	}

	if (!checkShaderFileDataValid(fileData)) {
		fprintf(stderr, "missing shader files\n");
		return -1;
	}

	ShaderBinaryData binaryData = { 0 };

	shaderc_compiler_t compiler = shaderc_compiler_initialize();
	shaderc_compile_options_t options = shaderc_compile_options_initialize();
	shaderc_compile_options_set_optimization_level(options, optimisationLevel == 0 ? shaderc_optimization_level_zero : shaderc_optimization_level_performance);

	int total = (fileData.vertFile != 0) + (fileData.fragFile != 0) + (fileData.compFile != 0);
	int succeeded = 0;
	succeeded += processShader(fileData.vertFile, &binaryData.vertBinary, shaderc_glsl_vertex_shader, compiler, options);
	succeeded += processShader(fileData.fragFile, &binaryData.fragBinary, shaderc_glsl_fragment_shader, compiler, options);
	succeeded += processShader(fileData.compFile, &binaryData.compBinary, shaderc_glsl_compute_shader, compiler, options);
	
	shaderc_compile_options_release(options);
	shaderc_compiler_release(compiler);

	if (succeeded != total) {
		fprintf(stderr, "compilation errors\n");
	}
	if(total != 1){
		printf("%i of %i succeeded compilation\n", succeeded, total);
	}else{
		printf("Compilation succeeded");
	}
	char* shaderName;
	int shaderNameLength = 0;
	printf("generating %s\n", fileData.outputFile);
	getShaderName(fileData.outputFile, &shaderNameLength, &shaderName);


	printf("Generating header file for %s\n", shaderName);

	FILE* file = fopen(fileData.outputFile, "w");
	if (!file) {
		fprintf(stderr, "unable to open file %s\n", fileData.outputFile);
	}

	
	unsigned long long fileHash = hash(shaderName);
	

	fprintf(file, "#ifndef __%llX_GUARD__\n#define __%llX_GUARD__\n\n//THIS FILE IS AUTOMATICALLY GENERATED - DO NOT EDIT\n\n", fileHash, fileHash);
	if (fileData.vertFile) {
		printShader(file, shaderName, &binaryData.vertBinary, shaderc_glsl_vertex_shader);
		fprintf(file, "\n\n");
	}
	if (fileData.fragFile) {
		printShader(file, shaderName, &binaryData.fragBinary, shaderc_glsl_fragment_shader);
		fprintf(file, "\n\n");
	}
	if (fileData.compFile) {
		printShader(file, shaderName, &binaryData.compBinary, shaderc_glsl_compute_shader);
		fprintf(file, "\n\n");
	}

	fprintf(file, "\n#else\n#ifdef CHECK_HEADER_GUARD_COLLISIONS\n#error \"HEADER_COLLISION_DETECTED\"\n#endif\n#endif //__%llX_GUARD__\n", fileHash);
	fclose(file);

	printf("Generation successfull!\n");



	if (generateDepFile) {
		printf("generating dependency file\n");
		size_t pathSize = strlen(fileData.outputFile);
		size_t bufferSize = pathSize + 3;
		char* buffer = (char*)malloc(bufferSize);
		if (!buffer) {
			fprintf(stderr, "out of mem!\n");
			return -1;
		}
		memset(buffer, 0, bufferSize);
		memcpy(buffer, fileData.outputFile, pathSize);
		buffer[pathSize + 0] = '.';
		buffer[pathSize + 1] = 'd';
		buffer[pathSize + 2] = '\0';

		FILE* depFile = fopen(buffer, "w");
		fprintf(depFile, "%s: ", fileData.outputFile);
		
		if (fileData.vertFile) {
			if (fileData.fragFile || fileData.compFile) {
				fprintf(depFile, "%s\\\n", fileData.vertFile);
			} else {
				fprintf(depFile, "%s\n", fileData.vertFile);
			}
		}

		if (fileData.fragFile) {
			if (fileData.compFile) {
				fprintf(depFile, "  %s \\\n", fileData.fragFile);
			} else {
				fprintf(depFile, "  %s\n", fileData.fragFile);
			}
		}

		if (fileData.compFile) {
			fprintf(depFile, "  %s", fileData.compFile);
		}

		fclose(depFile);
		free(buffer);
		printf("generation complete\n");
	}

	return 0;
}