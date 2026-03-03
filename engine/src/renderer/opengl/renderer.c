#include "renderer/renderer.h"
#include "glad/include/glad/glad.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype/stb_truetype.h"

#include <AvUtils/avLogging.h>
#include <AvUtils/avMemory.h>
#include <AvUtils/avString.h>
#include <math.h>
#include <stdio.h>

#include "platform/rendererPlatform.h"

#define MAX_VERTICES 16384
#define MAX_INDICES  16384

#define FONT_SIZE 16.0f
#define BITMAP_W 512
#define BITMAP_H 512

stbtt_bakedchar cdata[96]; // ASCII 32..126

typedef struct Vertex2D{
    float x, y;
    float u, v;
    Color4f color;
} Vertex2D;

typedef struct VertexUV{
    float x, y;
    Color4f color;
} VertexUV;

typedef struct Renderer {
    GLuint vbo;
    GLuint vao;
    GLuint ebo;
    GLuint shader;
    float projection[16];
    GLuint projLocation;
    GLuint useTextureLoc;
    GLuint fontTexture;

    Vertex2D* vertexBuffer;
    uint32_t* indexBuffer;

    uint32_t vertexCount;
    uint32_t indexCount;

    void* rendererPlatform;
}Renderer;


Renderer* renderer;

void load_font(const char* path)
{
    // Read TTF file
    FILE* f = fopen(path, "rb");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    unsigned char* ttf_buffer = malloc(size);
    fread(ttf_buffer, 1, size, f);
    fclose(f);

    // Create bitmap atlas
    unsigned char bitmap[BITMAP_W * BITMAP_H];

    stbtt_BakeFontBitmap(
        ttf_buffer, 0,
        FONT_SIZE,
        bitmap,
        BITMAP_W, BITMAP_H,
        32, 96,
        cdata
    );

    free(ttf_buffer);

    // Upload bitmap as OpenGL texture
    glGenTextures(1, &renderer->fontTexture);
    glBindTexture(GL_TEXTURE_2D, renderer->fontTexture);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED,
                 BITMAP_W, BITMAP_H,
                 0, GL_RED, GL_UNSIGNED_BYTE,
                 bitmap);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_ONE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_ONE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_ONE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_RED);
}

void rendererShutdown(void* state){
    avFree(renderer->rendererPlatform);
}

void mat4_ortho(float* m, float l, float r, float b, float t, float n, float f)
{
    avMemset(m, 0, sizeof(float) * 16);

    m[0]  = 2.0f / (r - l);
    m[5]  = 2.0f / (t - b);
    m[10] = -2.0f / (f - n);
    m[12] = -(r + l) / (r - l);
    m[13] = -(t + b) / (t - b);
    m[14] = -(f + n) / (f - n);
    m[15] = 1.0f;
}

// const char* vertexShader = "#version 330 core\nlayout (location = 0) in vec2 in_pos;\nlayout (location = 1) in vec4 in_color;\nuniform mat4 u_projection;\nout vec4 frag_color;\nvoid main(){\nfrag_color = in_color;\ngl_Position = u_projection * vec4(in_pos, 0.0, 1.0);\n}";
// const char* fragmentShader = "#version 330 core\nin vec4 frag_color;\nout vec4 out_color;\nvoid main(){\nout_color = frag_color;\n}";


const char* vertexShader = "#version 330 core\nlayout (location = 0) in vec2 in_pos;\n layout (location = 1) in vec2 in_uv;\nlayout (location = 2) in vec4 in_color;\nuniform mat4 u_projection;\nout vec2 frag_uv;\nout vec4 frag_color;\nvoid main(){\nfrag_color = in_color;\ngl_Position = u_projection * vec4(in_pos, 0.0, 1.0);\nfrag_uv=in_uv;\n}";
const char* fragmentShader = "#version 330 core\nin vec2 frag_uv;\nin vec4 frag_color;\n\nout vec4 out_color;\nuniform sampler2D u_texture;\nuniform int u_useTexture;\nvoid main(){\nvec4 tex = u_useTexture == 1 ? vec4(texture(u_texture, frag_uv)) : vec4(1.0);\nout_color = frag_color * tex;\n}";


void rendererStartup(uint64* memoryRequirement, void* state, void* platformState){
    (*memoryRequirement) = sizeof(Renderer);
    if(state==0){
        return;
    }
    renderer = (Renderer*)state;

    uint64 mem = 0;
    platformRendererStartup(&mem, 0, 0);
    renderer->rendererPlatform = avAllocate(mem, "");
    platformRendererStartup(&mem, renderer->rendererPlatform, platformState);
    platformChoosePixelFormat();
    platformCreateGlContext();

    if(!gladLoadGL()){
        avAssert(0, "Failed to load Opengl");
    }

    glGenVertexArrays(1, &renderer->vao);
    glGenBuffers(1, &renderer->vbo);
    glGenBuffers(1, &renderer->ebo);

    glBindVertexArray(renderer->vao);

    glBindBuffer(GL_ARRAY_BUFFER, renderer->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex2D) * 4, NULL, GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer->ebo);

    
    // position
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D), (void*)0);
    glEnableVertexAttribArray(0);

    // uv
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D), (void*)(sizeof(float)*2));
    glEnableVertexAttribArray(1);

    // color
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex2D), (void*)(sizeof(float)*4));
    glEnableVertexAttribArray(2);

    // load shaders
    GLuint vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &vertexShader, NULL);
    glCompileShader(vertex);
    int success;
    glGetShaderiv(vertex, GL_COMPILE_STATUS, &success);
    if(!success){
        char infoLog[512];
        glGetShaderInfoLog(vertex, 512, NULL, infoLog);
        avStringPrintf(AV_CSTR("%s\n"), infoLog);
        avAssert(0, "Compilation failed");
    }

    GLuint fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &fragmentShader, NULL);
    glCompileShader(fragment);
    glGetShaderiv(fragment, GL_COMPILE_STATUS, &success);
    if(!success){
        char infoLog[512];
        glGetShaderInfoLog(fragment, 512, NULL, infoLog);
        avStringPrintf(AV_CSTR("%s\n"), infoLog);
        avAssert(0, "Compilation failed");
    }

    renderer->shader = glCreateProgram();
    glAttachShader(renderer->shader, vertex);
    glAttachShader(renderer->shader, fragment);
    glLinkProgram(renderer->shader);
    glGetProgramiv(renderer->shader, GL_LINK_STATUS, &success);
    if(!success){
        char infoLog[512];
        glGetProgramInfoLog(renderer->shader, 512, NULL, infoLog);
        avStringPrintf(AV_CSTR("%s\n"), infoLog);
        avAssert(0, "Linking failed");
    }

    glDeleteShader(vertex);
    glDeleteShader(fragment);

    renderer->projLocation = glGetUniformLocation(renderer->shader, "u_projection");
    renderer->useTextureLoc = glGetUniformLocation(renderer->shader, "u_useTexture");

    unsigned int indices[6] = {
        0,1,2,
        2,1,3
    };

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer->ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    renderer->vertexBuffer = avAllocate(sizeof(Vertex2D) * MAX_VERTICES, "");
    renderer->indexBuffer  = avAllocate(sizeof(uint32_t) * MAX_INDICES, "");

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    //texture
    load_font("C:\\Windows\\Fonts\\Arial.ttf");

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    
}

static void rendererPushVertex(Vertex2D v) {
    renderer->vertexBuffer[renderer->vertexCount++] = v;
}

static void rendererPushIndex(uint32_t i) {
    renderer->indexBuffer[renderer->indexCount++] = i;
}

static void rendererFlush(){
    if (renderer->indexCount == 0){
            return;
    }

    glBindBuffer(GL_ARRAY_BUFFER, renderer->vbo);
    glBufferData(GL_ARRAY_BUFFER,
                renderer->vertexCount * sizeof(Vertex2D),
                renderer->vertexBuffer,
                GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer->ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                renderer->indexCount * sizeof(uint32_t),
                renderer->indexBuffer,
                GL_DYNAMIC_DRAW);

    glBindVertexArray(renderer->vao);
    glDrawElements(GL_TRIANGLES,
                renderer->indexCount,
                GL_UNSIGNED_INT,
                0);

    renderer->vertexCount = 0;
    renderer->indexCount = 0;
}

static void ensureCapacity(uint32_t vCount, uint32_t iCount){
    if (renderer->vertexCount + vCount >= MAX_VERTICES ||
        renderer->indexCount  + iCount >= MAX_INDICES){
        rendererFlush();
    }
}


void rendererBeginFrame(int width, int height){
    glViewport(0, 0, width, height);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    mat4_ortho(renderer->projection, 0, width, height, 0, -1, 1);
    
    glUseProgram(renderer->shader);
    glUniformMatrix4fv(renderer->projLocation, 1, GL_FALSE, renderer->projection);

}

void rendererDrawRect(float x, float y, float w, float h, Color4f color) {
    ensureCapacity(4, 6);

    uint32_t start = renderer->vertexCount;

    rendererPushVertex((Vertex2D){ x,     y,     0.0f, 0.0f, color });
    rendererPushVertex((Vertex2D){ x+w,   y,     1.0f, 0.0f, color });
    rendererPushVertex((Vertex2D){ x,     y+h,   0.0f, 1.0f, color });
    rendererPushVertex((Vertex2D){ x+w,   y+h,   1.0f, 1.0f, color });

    rendererPushIndex(start + 0);
    rendererPushIndex(start + 1);
    rendererPushIndex(start + 2);
    rendererPushIndex(start + 2);
    rendererPushIndex(start + 1);
    rendererPushIndex(start + 3);
}

void rendererDrawTriangle(float x1, float y1, float x2, float y2, float x3, float y3, Color4f color) {
    ensureCapacity(3, 3);
    uint32_t start = renderer->vertexCount;

    rendererPushVertex((Vertex2D){ x1, y1, 0.0f, 0.0f, color });
    rendererPushVertex((Vertex2D){ x2, y2, 0.0f, 0.0f, color });
    rendererPushVertex((Vertex2D){ x3, y3, 0.0f, 0.0f, color });

    rendererPushIndex(start + 0);
    rendererPushIndex(start + 1);
    rendererPushIndex(start + 2);
}

void rendererDrawLine(float x1, float y1, float x2, float y2, float thickness, Color4f color){
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = sqrtf(dx*dx + dy*dy);

    dx /= len;
    dy /= len;

    float px = -dy * thickness * 0.5f;
    float py =  dx * thickness * 0.5f;

    rendererDrawTriangle(x1+px, y1+py,
                         x2+px, y2+py,
                         x1-px, y1-py,
                         color);

    rendererDrawTriangle(x1-px, y1-py,
                         x2+px, y2+py,
                         x2-px, y2-py,
                         color);
}

void rendererDrawPolyline(float* points, int count, float thickness, Color4f color) {
    for(int i = 0; i < count - 1; ++i)
    {
        rendererDrawLine(
            points[i*2], points[i*2+1],
            points[(i+1)*2], points[(i+1)*2+1],
            thickness,
            color);
    }
}

void rendererDrawCircle(float cx, float cy, float radius, int segments, Color4f color) {
    ensureCapacity(1 + segments, 3*segments);
    uint32_t start = renderer->vertexCount;

    rendererPushVertex((Vertex2D){ cx, cy, 0.5f, 0.5f, color });

    for(int i = 0; i <= segments; i++)
    {
        float angle = (float)i / segments * 2.0f * 3.1415926f;
        float cos = cosf(angle);
        float sin = sinf(angle);
        float x = cx + cos * radius;
        float y = cy + sin * radius;

        rendererPushVertex((Vertex2D){ x, y, cos/2.0f+0.5f, sin/2.0f+0.5f, color });
    }

    for(int i = 1; i <= segments; i++)
    {
        rendererPushIndex(start);
        rendererPushIndex(start + i);
        rendererPushIndex(start + i + 1);
    }
}

void renderText(float x, float y, const char* str, Color4f color){
    rendererFlush();
    char c;
    stbtt_aligned_quad q;
    uint32 len = strlen(str);
    for(uint32 i = 0; i < len; i++){
        char c = str[i];
        stbtt_GetBakedQuad(cdata, 512, 512,
            c - 32,
            &x, &y,
            &q, 1);
        uint32_t start = renderer->vertexCount;
        rendererPushVertex((Vertex2D){ q.x0, q.y0, q.s0, q.t0, color });
        rendererPushVertex((Vertex2D){ q.x1, q.y0, q.s1, q.t0, color });
        rendererPushVertex((Vertex2D){ q.x0, q.y1, q.s0, q.t1, color });
        rendererPushVertex((Vertex2D){ q.x1, q.y1, q.s1, q.t1, color });

        rendererPushIndex(start + 0);
        rendererPushIndex(start + 1);
        rendererPushIndex(start + 2);
        rendererPushIndex(start + 2);
        rendererPushIndex(start + 1);
        rendererPushIndex(start + 3);
    }

    
    glBindTexture(GL_TEXTURE_2D, renderer->fontTexture);
    glUniform1i(renderer->useTextureLoc, 1);
    rendererFlush();
    glUniform1i(renderer->useTextureLoc, 0);
    
}





void rendererEndFrame(){
    
    rendererFlush();
    platformSwapBuffers();

}