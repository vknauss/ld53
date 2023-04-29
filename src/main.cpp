#include <cstdint>
#include <cstring>
#include <deque>
#include <fstream>
#include <iostream>
#include <vector>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define INSTANCES_PER_UNIFORM_BUFFER 256

struct UniformBufferInfo
{
    GLuint buffer = 0;
    uintptr_t offset = 0;
    size_t size = 0;
};

struct Transform
{
    glm::vec2 position = glm::vec2(0.0f);
    float rotation = 0.0f;

    glm::mat4 computeMatrix() const
    {
        glm::mat4 matrix;
        float cosAngle = std::cos(rotation);
        float sinAngle = std::sin(rotation);
        matrix[0] = glm::vec4(cosAngle, sinAngle, 0.0f, 0.0f);
        matrix[1] = glm::vec4(-sinAngle, cosAngle, 0.0f, 0.0f);
        matrix[2] = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
        matrix[3] = glm::vec4(position, 0.0f, 1.0f);

        return matrix;
    }
};

struct SceneGraphNode
{
    Transform local;
    Transform world;
    glm::vec2 size = glm::vec2(1.0f);
    glm::vec4 color = glm::vec4(1.0f);
    uint32_t parent = 0;
    std::vector<uint32_t> children;
    bool dirty = true;
};

struct DrawBatch
{
    std::vector<uint32_t> instances;
    GLuint texture = 0;
    UniformBufferInfo transformBufferInfo;
    UniformBufferInfo materialBufferInfo;
};

struct DrawLayer
{
    std::vector<DrawBatch> batches;
    glm::mat4 cameraMatrix;
};

struct GLFWWrapper
{
    GLFWWrapper()
    {
        if (!glfwInit())
        {
            throw std::runtime_error("glfwInit failed");
        }
    }

    ~GLFWWrapper()
    {
        glfwTerminate();
    }
};

static GLFWwindow* createWindow(int width, int height, const char* title)
{
    GLFWwindow* window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window)
    {
        throw std::runtime_error("Failed to create window");
    }
    glfwSetWindowSizeLimits(window, width, height, width, height);
    return window;
}

static GLuint loadShader(const char* filename, GLenum shaderType)
{
    std::ifstream fileStream(filename);
    if (!fileStream)
    {
        throw std::runtime_error("Failed to open file: " + std::string(filename));
    }

    const std::string fileAsString{ std::istreambuf_iterator<char>(fileStream), std::istreambuf_iterator<char>() };
    const char* source = fileAsString.c_str();
    GLuint shader = glCreateShader(shaderType);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint compileStatus;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compileStatus);
    if (!compileStatus)
    {
        GLint infoLogLength;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLogLength);
        std::vector<char> infoLog(infoLogLength);
        glGetShaderInfoLog(shader, infoLogLength, nullptr, infoLog.data());
        throw std::runtime_error("Failed to compile shader: " + std::string(filename) + " Info log: " + std::string(infoLog.data()));
    }

    return shader;
}

static GLuint createShaderProgram(const std::vector<GLuint>& shaders)
{
    GLuint program = glCreateProgram();

    for (const GLuint shader : shaders)
    {
        glAttachShader(program, shader);
    }

    glLinkProgram(program);

    for (const GLuint shader : shaders)
    {
        glDetachShader(program, shader);
    }

    GLint linkStatus;
    glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
    if (!linkStatus)
    {
        GLint infoLogLength;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLogLength);
        std::vector<char> infoLog(infoLogLength);
        glGetProgramInfoLog(program, infoLogLength, nullptr, infoLog.data());
        throw std::runtime_error("Failed to link program. Info log: " + std::string(infoLog.data()));
    }

    return program;
}

static GLuint loadTexture(const char* filename)
{
    int width, height, components;
    stbi_uc* pixelData = stbi_load(filename, &width, &height, &components, STBI_rgb_alpha);
    if (!pixelData)
    {
        throw std::runtime_error("Failed to load texture: " + std::string(filename));
    }

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixelData);
    glGenerateMipmap(GL_TEXTURE_2D);

    stbi_image_free(pixelData);

    return texture;
}

class SceneGraph
{
    std::vector<SceneGraphNode> nodes;
    std::deque<uint32_t> freeIndices;

    void removeParent(uint32_t index)
    {
        bool found = false;
        uint32_t childIndex = 0;
        SceneGraphNode& parent = nodes[nodes[index].parent];
        for (uint32_t i = 0; !found && i < parent.children.size(); ++i)
        {
            if (parent.children[i] == index)
            {
                childIndex = i;
                found = true;
            }
        }
        if (found && childIndex < parent.children.size() - 1)
        {
            parent.children[childIndex] = parent.children.back();
            parent.children.pop_back();
        }
    }

    void addParent(uint32_t index, uint32_t parent)
    {
        nodes[index].parent = parent;
        nodes[parent].children.push_back(index);
    }

    void setDirty(uint32_t index)
    {
        if (!nodes[index].dirty)
        {
            nodes[index].dirty = true;
            for (auto childIndex : nodes[index].children)
            {
                setDirty(childIndex);
            }
        }
    }

public:
    SceneGraph() : nodes(1) { }

    uint32_t create(uint32_t parent = 0)
    {
        uint32_t index;
        if (!freeIndices.empty())
        {
            index = freeIndices.front();
            freeIndices.pop_front();
        }
        else
        {
            index = nodes.size();
            nodes.emplace_back();
        }
        nodes[index].local.position = glm::vec2(0.0f);
        nodes[index].local.rotation = 0.0f;
        nodes[index].size = glm::vec2(1.0f);
        nodes[index].color = glm::vec4(1.0f);
        nodes[index].dirty = true;
        addParent(index, parent);

        return index;
    }

    void destroy(uint32_t index)
    {
        removeParent(index);
        freeIndices.push_back(index);
    }

    void setParent(uint32_t index, uint32_t parent)
    {
        if (nodes[index].parent != parent)
        {
            removeParent(index);
            addParent(index, parent);
            setDirty(index);
        }
    }

    void setSize(uint32_t index, const glm::vec2& size)
    {
        nodes[index].size = size;
    }

    void setColor(uint32_t index, const glm::vec4& color)
    {
        nodes[index].color = color;
    }

    void setPosition(uint32_t index, const glm::vec2& position)
    {
        nodes[index].local.position = position;
        setDirty(index);
    }

    void setRotation(uint32_t index, float rotation)
    {
        nodes[index].local.rotation = rotation;
        setDirty(index);
    }

    const glm::vec2& getSize(uint32_t index) const
    {
        return nodes[index].size;
    }

    const glm::vec4& getColor(uint32_t index) const
    {
        return nodes[index].color;
    }

    const Transform& getLocalTransform(uint32_t index) const
    {
        return nodes[index].local;
    }

    const Transform& getWorldTransform(uint32_t index)
    {
        SceneGraphNode& node = nodes[index];
        if (node.dirty)
        {
            if (node.parent != index)
            {
                const Transform& parentTransform = getWorldTransform(node.parent);
                float cosAngle = std::cos(parentTransform.rotation);
                float sinAngle = std::sin(parentTransform.rotation);
                node.world.position = parentTransform.position + glm::vec2(cosAngle * node.local.position.x - sinAngle * node.local.position.y, sinAngle * node.local.position.x + cosAngle * node.local.position.y);
                node.world.rotation = parentTransform.rotation + node.local.rotation;
            }
            else
            {
                node.world = node.local;
            }
            node.dirty = false;
        }

        return node.world;
    }

    glm::mat4 getRenderMatrix(uint32_t index)
    {
        const Transform& transform = getWorldTransform(index);
        glm::mat4 matrix = transform.computeMatrix();
        matrix[0] *= nodes[index].size.x;
        matrix[1] *= nodes[index].size.y;
        return matrix;
    }
};

class UniformBufferManager
{
    std::vector<UniformBufferInfo> bufferInfos;
    std::vector<uint32_t> inUseBuffers[2];
    std::vector<uint32_t> freeBuffers;
    int frameIndex = 0;
    uint32_t currentBuffer = 0;
    void* mappedPointer = nullptr;
    GLint uboAlignment = 1;

public:
    UniformBufferManager()
    {
        glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &uboAlignment);
    }

    virtual ~UniformBufferManager()
    {
        for (const auto& info : bufferInfos)
        {
            glDeleteBuffers(1, &info.buffer);
        }
    }

    void beginFrameUpload()
    {
        for (auto i : inUseBuffers[frameIndex])
        {
            freeBuffers.push_back(i);
        }
        inUseBuffers[frameIndex].clear();
    }

    void endFrameUpload()
    {
        if (mappedPointer)
        {
            glUnmapBuffer(GL_UNIFORM_BUFFER);
            mappedPointer = nullptr;
        }
        frameIndex = (frameIndex + 1) % 2;
    }

    void prepareUpload(size_t requiredSize, UniformBufferInfo& rangeInfo)
    {
        UniformBufferInfo* info = nullptr;
        if (currentBuffer < bufferInfos.size())
        {
            info = &bufferInfos[currentBuffer];
            info->offset = (info->offset + uboAlignment - 1) & ~(uboAlignment - 1);
            if (!info->offset + requiredSize < info->size)
            {
                glUnmapBuffer(GL_UNIFORM_BUFFER);
                mappedPointer = nullptr;
                info = nullptr;
            }
        }
        if (!info)
        {
            if (!freeBuffers.empty())
            {
                currentBuffer = freeBuffers.back();
                freeBuffers.pop_back();
                info = &bufferInfos[currentBuffer];
                info->offset = 0;
                glBindBuffer(GL_UNIFORM_BUFFER, info->buffer);
            }
            else
            {
                currentBuffer = bufferInfos.size();
                info = &bufferInfos.emplace_back();
                info->offset = 0;
                info->size = INSTANCES_PER_UNIFORM_BUFFER * sizeof(glm::mat4);
                glGenBuffers(1, &info->buffer);
                glBindBuffer(GL_UNIFORM_BUFFER, info->buffer);
                glBufferData(GL_UNIFORM_BUFFER, info->size, NULL, GL_STREAM_DRAW);
            }
                
            inUseBuffers[frameIndex].push_back(currentBuffer);
            mappedPointer = glMapBuffer(GL_UNIFORM_BUFFER, GL_WRITE_ONLY);
            if (!mappedPointer)
            {
                throw std::runtime_error("Failed to map uniform buffer");
            }
        }

        rangeInfo.buffer = info->buffer;
        rangeInfo.offset = info->offset;
        rangeInfo.size = requiredSize;
    }

    void uploadData(const void* data, size_t size, size_t offset = 0)
    {
        memcpy(static_cast<char*>(mappedPointer) + bufferInfos[currentBuffer].offset, data, size);
        bufferInfos[currentBuffer].offset += offset ? offset : size;
    }
};

class TransformBufferManager : public UniformBufferManager
{
public:
    void updateDrawBatch(const glm::mat4& cameraMatrix, SceneGraph& sceneGraph, DrawBatch& batch)
    {
        if (batch.instances.size() > INSTANCES_PER_UNIFORM_BUFFER)
        {
            throw std::runtime_error("Batch instance count exceeds max per buffer");
        }

        size_t requiredSize = batch.instances.size() * sizeof(glm::mat4);
        prepareUpload(requiredSize, batch.transformBufferInfo);

        glm::mat4 matrix;
        for (const auto& instance : batch.instances)
        {
            matrix = sceneGraph.getRenderMatrix(instance);
            matrix = cameraMatrix * matrix;
            uploadData(glm::value_ptr(matrix), sizeof(matrix));
        }
    }
};

class MaterialBufferManager : public UniformBufferManager
{
public:
    void updateDrawBatch(SceneGraph& sceneGraph, DrawBatch& batch)
    {
        if (batch.instances.size() > INSTANCES_PER_UNIFORM_BUFFER)
        {
            throw std::runtime_error("Batch instance count exceeds max per buffer");
        }

        size_t requiredSize = batch.instances.size() * 2 * sizeof(glm::vec4);
        prepareUpload(requiredSize, batch.materialBufferInfo);

        int useTexture = (batch.texture != 0);
        for (const auto& instance : batch.instances)
        {
            uploadData(glm::value_ptr(sceneGraph.getColor(instance)), sizeof(glm::vec4));
            uploadData(&useTexture, sizeof(int), sizeof(glm::vec4));
        }
    }
};

int main(int argc, char** argv)
{
    GLFWWrapper glfwWrapper;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    GLFWwindow* window = createWindow(1920, 1080, "Window");
    glfwMakeContextCurrent(window);

    if (!gladLoadGL())
    {
        throw std::runtime_error("gladLoadGL failed");
    }

    GLuint shaderProgram = 0;
    {
        GLuint vertexShader = loadShader("shaders/vertex.glsl", GL_VERTEX_SHADER);
        GLuint fragmentShader = loadShader("shaders/fragment.glsl", GL_FRAGMENT_SHADER);
        shaderProgram = createShaderProgram({ vertexShader, fragmentShader });
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
    }

    glUniformBlockBinding(shaderProgram, glGetUniformBlockIndex(shaderProgram, "TransformData"), 0);
    glUniformBlockBinding(shaderProgram, glGetUniformBlockIndex(shaderProgram, "MaterialData"), 1);

    glUniform1i(glGetUniformLocation(shaderProgram, "textureSampler"), 0);

    GLuint vertexArray;
    glGenVertexArrays(1, &vertexArray);
    glBindVertexArray(vertexArray);

    TransformBufferManager transformBufferManager;
    MaterialBufferManager materialBufferManager;

    float cameraViewHeight = 20.0f;
    glm::vec2 cameraPosition(0, 0);

    std::vector<GLuint> textures;
    std::vector<DrawBatch> drawBatches;
    const uint32_t backgroundBatch = drawBatches.size();
    {
        auto& batch = drawBatches.emplace_back();
        batch.texture = textures.emplace_back(loadTexture("textures/character.png"));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    const uint32_t characterBatch = drawBatches.size();
    {
        auto& batch = drawBatches.emplace_back();
        batch.texture = textures.emplace_back(loadTexture("textures/character.png"));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }

    SceneGraph sceneGraph;

    uint32_t player = sceneGraph.create();
    sceneGraph.setSize(player, { 1, 2 });
    sceneGraph.setColor(player, { 1, 1, 1, 1 });
    drawBatches[characterBatch].instances.push_back(player);

    float speed = 5.0f;
    
    uint64_t timerValue = glfwGetTimerValue();


    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        float dt;
        {
            uint64_t previousTimer = timerValue;
            timerValue = glfwGetTimerValue();
            dt = static_cast<float>(static_cast<double>(timerValue - previousTimer) / static_cast<double>(glfwGetTimerFrequency()));
        }

        int windowWidth, windowHeight;
        glfwGetFramebufferSize(window, &windowWidth, &windowHeight);

        double cursorX, cursorY;
        glfwGetCursorPos(window, &cursorX, &cursorY);
        std::cout << cursorX << " " << cursorY << std::endl;

        glm::vec2 moveInput(0.0f);
        if (glfwGetKey(window, GLFW_KEY_W))
        {
            moveInput.y += 1;
        }
        if (glfwGetKey(window, GLFW_KEY_S))
        {
            moveInput.y -= 1;
        }
        if (glfwGetKey(window, GLFW_KEY_A))
        {
            moveInput.x -= 1;
        }
        if (glfwGetKey(window, GLFW_KEY_D))
        {
            moveInput.x += 1;
        }

        if (glm::dot(moveInput, moveInput) > 0.0f)
        {
            sceneGraph.setPosition(player, sceneGraph.getLocalTransform(player).position + speed * dt * glm::normalize(moveInput));
        }

        float aspectRatio = static_cast<float>(windowWidth) / static_cast<float>(windowHeight);
        glm::vec2 cameraViewHalfExtents(0.5f * aspectRatio * cameraViewHeight, 0.5f * cameraViewHeight);
        glm::mat4 cameraMatrix = glm::ortho(cameraPosition.x - cameraViewHalfExtents.x, cameraPosition.x + cameraViewHalfExtents.x, cameraPosition.y - cameraViewHalfExtents.y, cameraPosition.y + cameraViewHalfExtents.y);

        transformBufferManager.beginFrameUpload();
        for (auto& batch : drawBatches)
        {
            transformBufferManager.updateDrawBatch(cameraMatrix, sceneGraph, batch);
        }
        transformBufferManager.endFrameUpload();

        materialBufferManager.beginFrameUpload();
        for (auto& batch : drawBatches)
        {
            materialBufferManager.updateDrawBatch(sceneGraph, batch);
        }
        materialBufferManager.endFrameUpload();

        glViewport(0, 0, windowWidth, windowHeight);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(shaderProgram);

        GLuint boundTransformBuffer = 0;
        GLuint boundMaterialBuffer = 0;
        for (const auto& batch : drawBatches)
        {
            if (batch.transformBufferInfo.buffer != boundTransformBuffer)
            {
                glBindBufferRange(GL_UNIFORM_BUFFER, 0, batch.transformBufferInfo.buffer, batch.transformBufferInfo.offset, batch.transformBufferInfo.size);
                boundTransformBuffer = batch.transformBufferInfo.buffer;
            }

            if (batch.materialBufferInfo.buffer != boundMaterialBuffer)
            {
                glBindBufferRange(GL_UNIFORM_BUFFER, 1, batch.materialBufferInfo.buffer, batch.materialBufferInfo.offset, batch.materialBufferInfo.size);
                boundMaterialBuffer = batch.materialBufferInfo.buffer;
            }

            glBindTexture(GL_TEXTURE_2D, batch.texture);
            glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, batch.instances.size());
        }

        glfwSwapBuffers(window);
    }

    for (const auto& texture : textures)
    {
        glDeleteTextures(1, &texture);
    }
    glDeleteVertexArrays(1, &vertexArray);
    glDeleteProgram(shaderProgram);

    return 0;
}
