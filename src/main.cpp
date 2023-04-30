#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/random.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/string_cast.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "ecs.hpp"

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
    float depth = 0.0f;

    glm::mat4 computeMatrix() const
    {
        glm::mat4 matrix;
        float cosAngle = std::cos(rotation);
        float sinAngle = std::sin(rotation);
        matrix[0] = glm::vec4(cosAngle, sinAngle, 0.0f, 0.0f);
        matrix[1] = glm::vec4(-sinAngle, cosAngle, 0.0f, 0.0f);
        matrix[2] = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
        matrix[3] = glm::vec4(position, depth, 1.0f);

        return matrix;
    }
};

struct SceneGraphNode
{
    Transform local;
    Transform world;
    uint32_t parent = 0;
    std::vector<uint32_t> children;
    bool dirty = true;
};

struct DrawInstance
{
    glm::vec2 size = glm::vec2(1.0f);
    glm::vec4 color = glm::vec4(1.0f);
    // uint32_t drawBatch;
    GLuint texture;
    bool flipHorizontal = false;
};

struct DrawBatch
{
    uint32_t firstInstance;
    uint32_t instanceCount;
    GLuint texture = 0;
    UniformBufferInfo transformBufferInfo;
    UniformBufferInfo materialBufferInfo;
};

struct DrawLayer
{
    std::vector<DrawBatch> batches;
    glm::mat4 cameraMatrix;
};

struct Player
{
    uint32_t weapon;
    uint32_t backShoulder;
    uint32_t backHand;
    uint32_t frontShoulder;
    uint32_t frontHand;
};

struct Weapon
{
    enum class State
    {
        Idle, Swing
    };
    uint32_t owner;
    uint32_t armPivot;
    State state;
    uint64_t stateTimer;
    bool sharp;
    float damage;

    std::vector<float> poseAngles;
    std::vector<float> poseTimes;
    std::vector<bool> poseSharp;
};

struct Enemy
{
    enum class State
    {
        Idle, Hunting
    };
    float speed;
    State state = State::Idle;
    glm::vec2 moveInput;
};

struct Health
{
    enum class State
    {
        Normal, Invincible
    };
    float value;
    float max;
    glm::vec4 healthyColor;
    glm::vec4 damagedColor;
    glm::vec4 invincibleColor;
    State state;
    uint64_t stateTimer;
    bool takingDamage;
};

struct Dynamic
{
    float mass = 0.0f;
    float damping = 0.0f;
    glm::vec2 velocity = { 0.0f, 0.0f };
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

class SceneGraph : public ComponentManager<SceneGraphNode>
{
    void removeParent(uint32_t index)
    {
        bool found = false;
        uint32_t childIndex = 0;
        SceneGraphNode& parent = get(get(index).parent);
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
        auto& node = get(index);
        node.parent = parent;
        get(node.parent).children.push_back(index);
    }

    void setDirty(uint32_t index)
    {
        auto& node = get(index);
        if (!node.dirty)
        {
            node.dirty = true;
            for (auto childIndex : node.children)
            {
                setDirty(childIndex);
            }
        }
    }

    virtual SceneGraphNode& get(uint32_t index) override
    {
        return ComponentManager::get(index);
    }

    virtual const SceneGraphNode& get(uint32_t index) const override
    {
        return ComponentManager::get(index);
    }

    virtual const std::vector<SceneGraphNode>& all() const override
    {
        return ComponentManager::all();
    }

public:
    SceneGraph()
    {
        create(0); // it's helpful to have the 0 entity (which EntityManager will not return) have a valid node, so we can parent top-level nodes to it
    }

    void create(uint32_t index) override
    {
        create(index, 0);
    }

    void create(uint32_t index, uint32_t parent)
    {
        ComponentManager::create(index);
        auto& node = get(index);
        node.local.position = glm::vec2(0.0f);
        node.local.rotation = 0.0f;
        node.dirty = true;
        addParent(index, parent);
    }

    void destroy(uint32_t index) override
    {
        removeParent(index);
        ComponentManager::destroy(index);
    }

    void setParent(uint32_t index, uint32_t parent)
    {
        if (get(index).parent != parent)
        {
            removeParent(index);
            addParent(index, parent);
            setDirty(index);
        }
    }

    void setPosition(uint32_t index, const glm::vec2& position)
    {
        get(index).local.position = position;
        setDirty(index);
    }

    void setRotation(uint32_t index, float rotation)
    {
        get(index).local.rotation = rotation;
        setDirty(index);
    }

    void setDepth(uint32_t index, float depth)
    {
        get(index).local.depth = depth;
        setDirty(index);
    }

    const Transform& getLocalTransform(uint32_t index) const
    {
        return get(index).local;
    }

    const Transform& getWorldTransform(uint32_t index)
    {
        SceneGraphNode& node = get(index);
        if (node.dirty)
        {
            if (node.parent != index)
            {
                const Transform& parentTransform = getWorldTransform(node.parent);
                float cosAngle = std::cos(parentTransform.rotation);
                float sinAngle = std::sin(parentTransform.rotation);
                node.world.position = parentTransform.position + glm::vec2(cosAngle * node.local.position.x - sinAngle * node.local.position.y, sinAngle * node.local.position.x + cosAngle * node.local.position.y);
                node.world.rotation = parentTransform.rotation + node.local.rotation;
                node.world.depth = parentTransform.depth + node.local.depth;
            }
            else
            {
                node.world = node.local;
            }
            node.dirty = false;
        }

        return node.world;
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
    void updateDrawBatch(const glm::mat4& cameraMatrix, SceneGraph& sceneGraph, const ComponentManager<DrawInstance>& drawInstances, const std::vector<uint32_t>& indices, DrawBatch& batch)
    {
        if (batch.instanceCount > INSTANCES_PER_UNIFORM_BUFFER)
        {
            throw std::runtime_error("Batch instance count exceeds max per buffer");
        }

        size_t requiredSize = batch.instanceCount * sizeof(glm::mat4);
        prepareUpload(requiredSize, batch.transformBufferInfo);

        glm::mat4 matrix;
        for (uint32_t i = 0; i < batch.instanceCount; ++i)
        {
            auto index = indices[batch.firstInstance + i];
            const auto& instance = drawInstances.get(index);
            matrix = sceneGraph.getWorldTransform(index).computeMatrix();
            matrix[0] *= instance.flipHorizontal ? -instance.size.x : instance.size.x;
            matrix[1] *= instance.size.y;
            matrix = cameraMatrix * matrix;
            uploadData(glm::value_ptr(matrix), sizeof(matrix));
        }
    }
};

class MaterialBufferManager : public UniformBufferManager
{
public:
    void updateDrawBatch(SceneGraph& sceneGraph, const ComponentManager<DrawInstance>& drawInstances, const std::vector<uint32_t>& indices, DrawBatch& batch)
    {
        if (batch.instanceCount > INSTANCES_PER_UNIFORM_BUFFER)
        {
            throw std::runtime_error("Batch instance count exceeds max per buffer");
        }

        size_t requiredSize = batch.instanceCount * 2 * sizeof(glm::vec4);
        prepareUpload(requiredSize, batch.materialBufferInfo);

        int useTexture = (batch.texture != 0);
        for (uint32_t i = 0; i < batch.instanceCount; ++i)
        {
            auto index = indices[batch.firstInstance + i];
            uploadData(glm::value_ptr(drawInstances.get(index).color), sizeof(glm::vec4));
            uploadData(&useTexture, sizeof(int), sizeof(glm::vec4));
        }
    }
};

class Renderer
{
    TransformBufferManager transformBufferManager;
    MaterialBufferManager materialBufferManager;
    std::vector<DrawBatch> batches;
    std::vector<uint32_t> sortIndices;
public:
    void prepareRender(SceneGraph& sceneGraph, const ComponentManager<DrawInstance>& drawInstances, const glm::mat4& cameraMatrix)
    {
        sortIndices.assign(drawInstances.indices().begin(), drawInstances.indices().end());
        std::sort(sortIndices.begin(), sortIndices.end(), [&] (auto index0, auto index1)
                {
                    return sceneGraph.getWorldTransform(index0).depth < sceneGraph.getWorldTransform(index1).depth;
                });

        batches.clear();
        for (uint32_t i = 0; i < sortIndices.size(); ++i)
        {
            auto texture = drawInstances.get(sortIndices[i]).texture;
            if (batches.empty() || batches.back().texture != texture)
            {
                auto& batch = batches.emplace_back();
                batch.texture = texture;
                batch.firstInstance = i;
                batch.instanceCount = 0;
            }
            ++batches.back().instanceCount;
        }

        transformBufferManager.beginFrameUpload();
        for (auto& batch : batches)
        {
            transformBufferManager.updateDrawBatch(cameraMatrix, sceneGraph, drawInstances, sortIndices, batch);
        }
        transformBufferManager.endFrameUpload();

        materialBufferManager.beginFrameUpload();
        for (auto& batch : batches)
        {
            materialBufferManager.updateDrawBatch(sceneGraph, drawInstances, sortIndices, batch);
        }
        materialBufferManager.endFrameUpload();
    }

    void render()
    {
        GLuint boundTransformBuffer = 0;
        GLuint boundMaterialBuffer = 0;
        for (const auto& batch : batches)
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
            glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, batch.instanceCount);
        }
    }
};

static glm::mat4 rotationMat2(float rotation)
{
    float cosAngle = std::cos(rotation);
    float sinAngle = std::sin(rotation);
    return glm::mat2(cosAngle, sinAngle, -sinAngle, cosAngle);
}

struct CollisionRecord
{
    uint32_t index0, index1;
    float depth;
    glm::vec2 axis;
};

static CollisionRecord collideBoxes(const Transform& transform0, const glm::vec2& e0, const Transform& transform1, const glm::vec2& e1)
{
    glm::mat2 r0 = rotationMat2(transform0.rotation);
    glm::mat2 r1 = rotationMat2(transform1.rotation);
    glm::mat2 r0t = glm::transpose(r0);
    glm::mat2 r1t = glm::transpose(r1);
    glm::vec2 d = transform1.position - transform0.position;
    glm::vec2 d0 = r0t * d;  // d relative to transform0
    glm::vec2 d1 = r1t * d;  // d relative to transform1

    // first test b0 axes
    // the rotation matrix from b1 space to b0 space is r0t * r1
    // by absing this matrix, we can get the length of the projections of b1 onto the axes of b0
    glm::mat2 r1to0 = r0t * r1;
    glm::mat2 ar10(glm::abs(r1to0[0]), glm::abs(r1to0[1]));
    glm::vec2 e10 = ar10 * e1;  // the half extents of b1 in the rotated frame of b0
    
    CollisionRecord result {};
    // result.b0_reference = true;

    // test b0 x axis
    auto depth = result.depth = std::abs(d0.x) - e0.x - e10.x;
    result.axis = r0[0];
    // result.reference_face = face_id::pos_x;
    // result.reference_axis = axis_id::x;
    if (d0.x < 0) {
        result.axis = -result.axis;
        // result.reference_face = face_id::neg_x;
    }
    if (depth > 0) {  // separating axis found
        return result;
    }

    // test b0 y axis
    depth = std::abs(d0.y) - e0.y - e10.y;
    if (depth > result.depth) {
        result.depth = depth;
        result.axis = r0[1];
        /* result.reference_face = face_id::pos_y;
        result.reference_axis = axis_id::y; */
        if (d0.y < 0) {
            result.axis = -result.axis;
            // result.reference_face = face_id::neg_y;
        }
        if (depth > 0) return result;
    }

    // test b1 axes
    // we can just transpose ar10 to get abs(r1t * r0)
    glm::mat2 ar01 = glm::transpose(ar10);
    glm::vec2 e01 = ar01 * e0;

    // test b1 x axis
    depth = std::abs(d1.x) - e1.x - e01.x;
    if (depth > result.depth) {
        result.depth = depth;
        /* result.b0_reference = false;
        result.reference_axis = axis_id::x; */
        result.axis = -r1[0];
        // result.reference_face = face_id::pos_x;  // inverted since d points from 0 to 1
        if (d1.x > 0) {
            result.axis = -result.axis;
            // result.reference_face = face_id::neg_x;
        }
        if (depth > 0) return result;
    }

    // test b1 y axis
    depth = std::abs(d1.y) - e1.y - e01.y;
    if (depth > result.depth) {
        result.depth = depth;
        // result.b0_reference = false;
        // result.reference_axis = axis_id::y;
        result.axis = -r1[1];
        // result.reference_face = face_id::pos_y;  // inverted since d points from 0 to 1
        if (d1.y > 0) {
            result.axis = -result.axis;
            // result.reference_face = face_id::neg_y;
        }
    }

    return result;
}

struct ColliderInfo
{
    glm::vec2 halfExtents;
};

class CollisionWorld : public ComponentManager<ColliderInfo>
{
    std::vector<CollisionRecord> collisionRecords;

public:
    void update(SceneGraph& sceneGraph)
    {
        collisionRecords.clear();
        for (uint32_t i = 0; i < all().size(); ++i)
        {
            uint32_t index0 = indices()[i];
            const ColliderInfo& collider0 = all()[i];
            for (uint32_t j = i + 1; j < all().size(); ++j)
            {
                uint32_t index1 = indices()[j];
                const ColliderInfo& collider1 = all()[j];
                auto result = collideBoxes(sceneGraph.getWorldTransform(index0), collider0.halfExtents, sceneGraph.getWorldTransform(index1), collider1.halfExtents);
                if (result.depth < 0)
                {
                    result.index0 = index0;
                    result.index1 = index1;
                    collisionRecords.push_back(result);
                }
            }
        }
    }

    const std::vector<CollisionRecord>& getCollisionRecords() const
    {
        return collisionRecords;
    }
};

static void updateVelocity(Dynamic& body, const glm::vec2& targetVelocity, float acceleration, float dt)
{
    glm::vec2 deltaV = targetVelocity - body.velocity;
    float dv = glm::length(deltaV);
    if (dv > acceleration * dt)
    {
        body.velocity += deltaV * (acceleration * dt) / dv;
    }
    else
    {
        body.velocity += deltaV;
    }
}

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

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

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

    SceneGraph sceneGraph;
    CollisionWorld collisionWorld;
    Renderer renderer;
    ComponentManager<Enemy> enemies;
    ComponentManager<Player> players;
    ComponentManager<Weapon> weapons;
    ComponentManager<Health> healthComponents;
    // DrawInstanceManager drawInstances;
    ComponentManager<DrawInstance> drawInstances;
    ComponentManager<Dynamic> dynamics;

    EntityManager entityManager;
    entityManager.addComponentManager(sceneGraph);
    entityManager.addComponentManager(collisionWorld);
    entityManager.addComponentManager(enemies);
    entityManager.addComponentManager(drawInstances);
    entityManager.addComponentManager(players);
    entityManager.addComponentManager(weapons);
    entityManager.addComponentManager(healthComponents);
    entityManager.addComponentManager(dynamics);

    float cameraViewHeight = 20.0f;
    glm::vec2 cameraPosition(0, 0);

    std::vector<GLuint> textures;

    auto characterTexture = textures.emplace_back(loadTexture("textures/character.png"));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    auto armTexture = textures.emplace_back(loadTexture("textures/arm.png"));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    for (auto i = 0; i < 100; ++i)
    {
        uint32_t index = entityManager.create();

        enemies.create(index);
        Enemy& enemy = enemies.get(index);
        enemy.speed = 2.0f;
        enemy.moveInput = glm::vec2(0.0f);
        enemy.state = Enemy::State::Idle;

        healthComponents.create(index);
        auto& health = healthComponents.get(index);
        health.healthyColor = { 0.5, 1.0, 0.7, 1.0 };
        health.damagedColor = { 0.6, 0.6, 0.1, 1.0 };
        health.invincibleColor = { 1.0, 0.0, 0.0 , 1.0};
        health.max = 10.0f;
        health.value = 10.0f;
        health.state = Health::State::Normal;

        drawInstances.create(index);
        auto& instance = drawInstances.get(index);
        instance.color = { 0.5, 1.0, 0.7, 1.0 };
        instance.size = { 1.0, 2.0 };
        instance.texture = characterTexture;

        sceneGraph.create(index);
        sceneGraph.setPosition(index, glm::linearRand(glm::vec2(-20), glm::vec2(20)));

        collisionWorld.create(index);
        auto& collider = collisionWorld.get(index);
        collider.halfExtents = { 0.5, 1.0 };

        dynamics.create(index);
        auto& body = dynamics.get(index);
        body.mass = 10.0f;
        body.damping = 0.1f;
    }

    uint32_t player = entityManager.create();
    {
        players.create(player);

        healthComponents.create(player);
        auto& health = healthComponents.get(player);
        health.healthyColor = { 1.0, 1.0, 1.0, 1.0 };
        health.damagedColor = { 0.6, 0.2, 0.3, 1.0 };
        health.invincibleColor = { 1.0, 0.0, 0.0 , 1.0};
        health.max = 100.0f;
        health.value = 100.0f;
        health.state = Health::State::Normal;

        drawInstances.create(player);
        auto& instance = drawInstances.get(player);
        instance.color = { 1.0, 1.0, 1.0, 1.0 };
        instance.size = { 1.0, 2.0 };
        instance.texture = characterTexture;

        sceneGraph.create(player);

        collisionWorld.create(player);
        auto& collider = collisionWorld.get(player);
        collider.halfExtents = { 0.5, 1.0 };

        dynamics.create(player);
        auto& body = dynamics.get(player);
        body.mass = 10.0f;
        body.damping = 0.1f;
    }

    {
        constexpr glm::vec2 frontShoulderPosition { .28125, 0.375 };
        constexpr glm::vec2 backShoulderPosition { -0.0625, 0.375 };
        constexpr glm::vec2 armDrawSize { 0.3125, 1.0 };
        constexpr float armLength = 0.75f;

        auto& playerComponent = players.get(player);
        playerComponent.frontShoulder = entityManager.create();
        sceneGraph.create(playerComponent.frontShoulder, player);
        sceneGraph.setPosition(playerComponent.frontShoulder, frontShoulderPosition);
        sceneGraph.setDepth(playerComponent.frontShoulder, 0.1f);

        {
            auto arm = entityManager.create();
            sceneGraph.create(arm, playerComponent.frontShoulder);
            sceneGraph.setPosition(arm, { 0, -armLength / 2 });
            drawInstances.create(arm);
            auto& instance = drawInstances.get(arm);
            instance.color = glm::vec4(1.0f);
            instance.size = armDrawSize;
            instance.texture = armTexture;
        }

        playerComponent.frontHand = entityManager.create();
        sceneGraph.create(playerComponent.frontHand, playerComponent.frontShoulder);
        sceneGraph.setPosition(playerComponent.frontHand, { 0, -armLength });
        sceneGraph.setRotation(playerComponent.frontHand, M_PI_2f);

        playerComponent.backShoulder = entityManager.create();
        sceneGraph.create(playerComponent.backShoulder, player);
        sceneGraph.setPosition(playerComponent.backShoulder, backShoulderPosition);
        sceneGraph.setDepth(playerComponent.backShoulder, -0.1f);

        {
            auto arm = entityManager.create();
            sceneGraph.create(arm, playerComponent.backShoulder);
            sceneGraph.setPosition(arm, { 0, -armLength / 2 });
            drawInstances.create(arm);
            auto& instance = drawInstances.get(arm);
            instance.color = glm::vec4(1.0f);
            instance.size = armDrawSize;
            instance.texture = armTexture;
            instance.flipHorizontal = true;
        }

        playerComponent.backHand = entityManager.create();
        sceneGraph.create(playerComponent.backHand, playerComponent.backShoulder);
        sceneGraph.setPosition(playerComponent.backHand, { 0, -armLength });
        sceneGraph.setRotation(playerComponent.backHand, M_PI_2f);

        playerComponent.weapon = entityManager.create();
        weapons.create(playerComponent.weapon);
        Weapon& weapon = weapons.get(playerComponent.weapon);
        weapon.poseAngles = { 0.0f, -M_PI_2f, M_PI_4f, 0.0f };
        weapon.poseTimes = { 0.0f, 0.1f, 0.2f, 0.45f };
        weapon.poseSharp = { false, true, false, false };
        weapon.state = Weapon::State::Idle;
        weapon.owner = player;
        weapon.armPivot = playerComponent.frontShoulder;
        drawInstances.create(playerComponent.weapon);
        auto& instance = drawInstances.get(playerComponent.weapon);
        instance.color = { 0.8, 0.8, 0.8, 1.0 };
        instance.size = { 0.1, 0.5 };
        instance.texture = 0;
        sceneGraph.create(playerComponent.weapon, playerComponent.frontHand);
        sceneGraph.setPosition(playerComponent.weapon, { 0, 0.25f });
        collisionWorld.create(playerComponent.weapon);
        auto& collider = collisionWorld.get(playerComponent.weapon);
        collider.halfExtents = { 0.05, 0.25f };
    }

    {
        uint32_t index = entityManager.create();
        drawInstances.create(index);
        auto& instance = drawInstances.get(index);
        instance.color = { 0.8, 0.3, 0.2, 1.0 };
        instance.size = { 3, 2 };
        instance.texture = 0;
        sceneGraph.create(index);
        sceneGraph.setPosition(index, { 0, -5 });
        collisionWorld.create(index);
        auto& collider = collisionWorld.get(index);
        collider.halfExtents = { 1.5, 1.0 };
        dynamics.create(index);
    }

    float playerSpeed = 5.0f;
    float playerAcceleration = 25.0f;
    
    uint64_t timerValue = glfwGetTimerValue();

    std::vector<uint32_t> died;

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

        glm::vec2 targetVelocity(0);
        if (glm::dot(moveInput, moveInput) > 0.0001)
        {
            targetVelocity = glm::normalize(moveInput) * playerSpeed;
        }
        updateVelocity(dynamics.get(player), targetVelocity, playerAcceleration, dt);

        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT))
        {
            auto& weapon = weapons.get(players.get(player).weapon);
            if (weapon.state == Weapon::State::Idle)
            {
                weapon.state = Weapon::State::Swing;
                weapon.stateTimer = timerValue;
            }
        }

        for (auto index : weapons.indices())
        {
            auto& weapon = weapons.get(index);
            float stateTime = static_cast<float>(static_cast<double>(timerValue - weapon.stateTimer) / static_cast<double>(glfwGetTimerFrequency()));
            if (weapon.state == Weapon::State::Swing)
            {
                uint32_t poseIndex = 1;
                for (; poseIndex < weapon.poseTimes.size() && stateTime > weapon.poseTimes[poseIndex]; ++poseIndex);
                if (poseIndex < weapon.poseTimes.size())
                {
                    float angle0 = weapon.poseAngles[poseIndex - 1];
                    float angle1 = weapon.poseAngles[poseIndex];
                    float time0 = weapon.poseTimes[poseIndex - 1];
                    float time1 = weapon.poseTimes[poseIndex];
                    weapon.sharp = weapon.poseSharp[poseIndex - 1];
                    sceneGraph.setRotation(weapon.armPivot, angle0 + (angle1 - angle0) * (stateTime - time0) / (time1 - time0));
                }
                else
                {
                    weapon.sharp = false;
                    weapon.state = Weapon::State::Idle;
                }
            }
        }

        for (auto index : enemies.indices())
        {
            auto& enemy = enemies.get(index);
            enemy.moveInput = glm::vec2(0);
            glm::vec2 toPlayer = sceneGraph.getWorldTransform(player).position - sceneGraph.getWorldTransform(index).position;
            if (glm::dot(toPlayer, toPlayer) < 25.0f)
            {
                enemy.state = Enemy::State::Hunting;
            }
            else
            {
                enemy.state = Enemy::State::Idle;
            }

            switch (enemy.state)
            {
                case Enemy::State::Idle:
                    // maybe later, not important
                    break;
                case Enemy::State::Hunting:
                {
                    constexpr uint32_t targetNearbyCount = 5;
                    constexpr float nearbyThreshold = 3.0f;
                    uint32_t nearby[targetNearbyCount];
                    float distance2s[targetNearbyCount];
                    uint32_t nearbyCount= 0;
                    for (auto index1 : enemies.indices())
                    {
                        if (index == index1)
                        {
                            continue;
                        }
                        glm::vec2 toOther = sceneGraph.getWorldTransform(index1).position - sceneGraph.getWorldTransform(index).position;
                        float distance2 = glm::dot(toOther, toOther);
                        if (distance2 < nearbyThreshold)
                        {
                            uint32_t insertIndex = 0;
                            for (; insertIndex < nearbyCount && distance2 > distance2s[insertIndex]; ++insertIndex);
                            if (insertIndex < nearbyCount)
                            {
                                nearbyCount = nearbyCount < targetNearbyCount ? nearbyCount + 1 : targetNearbyCount;
                                for (uint32_t i = nearbyCount - 1; i > insertIndex; --i)
                                {
                                    nearby[i] = nearby[i - 1];
                                    distance2s[i] = distance2s[i - 1];
                                }
                                nearby[insertIndex] = index1;
                                distance2s[insertIndex] = distance2;
                            }
                            else if (nearbyCount < targetNearbyCount)
                            {
                                nearby[nearbyCount] = index1;
                                distance2s[insertIndex] = distance2;
                                ++nearbyCount;
                            }
                        }
                    }
                    glm::vec2 nearbyRepelDirection(0);
                    for (uint32_t i = 0; i < nearbyCount; ++i)
                    {
                        glm::vec2 toOther = sceneGraph.getWorldTransform(nearby[i]).position - sceneGraph.getWorldTransform(index).position;
                        nearbyRepelDirection -= 1.0f * toOther / std::max(0.001f, distance2s[i]);
                    }
                    enemy.moveInput = toPlayer + nearbyRepelDirection;
                    break;
                }
                default:
                    break;
            }

            glm::vec2 targetVelocity(0);
            if (glm::dot(enemy.moveInput, enemy.moveInput) > 0.0001)
            {
                targetVelocity = glm::normalize(enemy.moveInput) * enemy.speed;
            }
            updateVelocity(dynamics.get(index), targetVelocity, 10.0f, dt);
        }

        for (auto index : dynamics.indices())
        {
            auto& body = dynamics.get(index);
            sceneGraph.setPosition(index, sceneGraph.getLocalTransform(index).position + body.velocity * dt);
            body.velocity -= body.damping * body.velocity * dt;
        }

        collisionWorld.update(sceneGraph);

        for (const auto& record : collisionWorld.getCollisionRecords())
        {
            if (dynamics.has(record.index0) && dynamics.has(record.index1))
            {
                auto& body0 = dynamics.get(record.index0);
                auto& body1 = dynamics.get(record.index1);
                bool body0Static = (std::abs(body0.mass) < 0.0001f);
                bool body1Static = (std::abs(body1.mass) < 0.0001f);
                if (body0Static != body1Static)
                {
                    if (body0Static)
                    {
                        sceneGraph.setPosition(record.index1, sceneGraph.getLocalTransform(record.index1).position - record.depth * record.axis);
                        glm::vec2 relativeVelocity = body1.velocity - body0.velocity;
                        relativeVelocity -= glm::dot(relativeVelocity, record.axis) * record.axis;
                        body1.velocity = relativeVelocity + body0.velocity;
                    }
                    else
                    {
                        sceneGraph.setPosition(record.index0, sceneGraph.getLocalTransform(record.index0).position + record.depth * record.axis);
                        glm::vec2 relativeVelocity = body0.velocity - body1.velocity;
                        relativeVelocity -= glm::dot(relativeVelocity, record.axis) * record.axis;
                        body0.velocity = relativeVelocity + body1.velocity;
                    }
                }
                else if (!body0Static)
                {
                    glm::vec2 impulse = 10.0f * record.depth * record.axis;
                    std::cout << glm::to_string(impulse) << std::endl;
                    body0.velocity += impulse / body0.mass;
                    body1.velocity -= impulse / body1.mass;
                }
            }
            for (int i = 0; i < 2; ++i)
            {
                uint32_t active = (i == 0) ? record.index0 : record.index1;
                uint32_t other = (i == 0) ? record.index1 : record.index0;

                if (weapons.has(active))
                {
                    const auto& weapon = weapons.get(active);
                    if (weapon.sharp && weapon.owner != other && healthComponents.has(other))
                    {
                        auto& health = healthComponents.get(other);
                        if (health.state != Health::State::Invincible)
                        {
                            health.value -= 1;
                            health.takingDamage = true;
                        }
                    }
                }
            }
        }

        died.clear();
        for (auto index : healthComponents.indices())
        {
            auto& health = healthComponents.get(index);
            if (health.value <= 0)
            {
                died.push_back(index);
                continue;
            }
            if (health.takingDamage)
            {
                health.state = Health::State::Invincible;
                health.stateTimer = timerValue;
                health.takingDamage = false;
            }
            if (health.state == Health::State::Invincible)
            {
                if (timerValue - health.stateTimer >= glfwGetTimerFrequency())
                {
                    health.state = Health::State::Normal;
                    health.stateTimer = timerValue;
                }
            }
            if (drawInstances.has(index))
            {
                auto& instance = drawInstances.get(index);
                if (health.state == Health::State::Invincible)
                {
                    instance.color = health.invincibleColor;
                }
                else
                {
                    instance.color = glm::mix(health.damagedColor, health.healthyColor, health.value / health.max);
                }
            }
        }

        for (auto index : died)
        {
            entityManager.destroy(index);
        }

        float aspectRatio = static_cast<float>(windowWidth) / static_cast<float>(windowHeight);
        glm::vec2 cameraViewHalfExtents(0.5f * aspectRatio * cameraViewHeight, 0.5f * cameraViewHeight);
        glm::mat4 cameraMatrix = glm::ortho(cameraPosition.x - cameraViewHalfExtents.x, cameraPosition.x + cameraViewHalfExtents.x, cameraPosition.y - cameraViewHalfExtents.y, cameraPosition.y + cameraViewHalfExtents.y);

        renderer.prepareRender(sceneGraph, drawInstances, cameraMatrix);

        glViewport(0, 0, windowWidth, windowHeight);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(shaderProgram);

        renderer.render();

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
