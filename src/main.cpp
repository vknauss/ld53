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
        matrix[3] = glm::vec4(position, 0.0f, 1.0f);

        return matrix;
    }
};

struct SceneGraphNode
{
    Transform local;
    Transform world;
    float heightForDepth = 0; // amount to subtract from y-coord to get to front edge of sprite, for depth sorting purposes. only applies for top-level
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

struct Character
{
    uint32_t weapon;
    uint32_t backShoulder;
    uint32_t backHand;
    uint32_t frontShoulder;
    uint32_t frontHand;
    std::vector<uint32_t> spriteIndices;
    bool flipHorizontal;
};

struct WeaponAnimation
{
    std::vector<float> poseAngles;
    std::vector<float> poseTimes;
    std::vector<bool> poseSharp;
};

struct WeaponDescription
{
    const WeaponAnimation* animation;
    float damage;
    glm::vec2 size;
    glm::vec4 color;
    GLuint texture;
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
    bool flipHorizontal;
    const WeaponAnimation* animation;
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
    float attackRechargeTime;
    float turnDelayTime;
    float turnDelayTimeAccumulator = 0;
    bool wantToFace = false;
};

struct Player
{
    float speed;
    float acceleration;
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
    uint32_t healthBar;
};

struct Hurtbox
{
    float multiplier;
    uint32_t owner;
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
        if (found)
        {
            if (childIndex < parent.children.size() - 1)
            {
                parent.children[childIndex] = parent.children.back();
            }
            parent.children.pop_back();
        }
        else
        {
            std::cerr << "Warning: didn't find child. Transform hierarchy data is suspect" << std::endl;
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

    void setHeightForDepth(uint32_t index, float heightForDepth)
    {
        get(index).heightForDepth = heightForDepth;
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
                if (node.parent == 0)
                {
                    node.world.depth -= (node.local.position.y - node.heightForDepth);
                }
            }
            else
            {
                node.world = node.local;
            }
            node.dirty = false;
        }

        return node.world;
    }

    void destroyHierarchy(EntityManager& entityManager, uint32_t index, int callLevel = 0)
    {
        SceneGraphNode& node = get(index);
        for (auto childIndex : node.children)
        {
            destroyHierarchy(entityManager, childIndex, callLevel + 1);
        }
        entityManager.destroy(index);
    }

    template<typename Fn, typename ... Args>
    decltype(auto) recurse(uint32_t index, const Fn& fn, const Args& ... args)
    {
        SceneGraphNode& node = get(index);
        for (auto childIndex : node.children)
        {
            recurse(childIndex, fn, args...);
        }
        return fn(index, args...);
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

static glm::mat2 rotationMat2(float rotation)
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

    // test b0 x axis
    auto depth = result.depth = std::abs(d0.x) - e0.x - e10.x;
    result.axis = r0[0];
    if (d0.x < 0)
    {
        result.axis = -result.axis;
    }
    if (depth > 0)
    {
        return result;
    }

    // test b0 y axis
    depth = std::abs(d0.y) - e0.y - e10.y;
    if (depth > result.depth)
    {
        result.depth = depth;
        result.axis = r0[1];
        if (d0.y < 0)
        {
            result.axis = -result.axis;
        }
        if (depth > 0)
        {
            return result;
        }
    }

    // test b1 axes
    // we can just transpose ar10 to get abs(r1t * r0)
    glm::mat2 ar01 = glm::transpose(ar10);
    glm::vec2 e01 = ar01 * e0;

    // test b1 x axis
    depth = std::abs(d1.x) - e1.x - e01.x;
    if (depth > result.depth)
    {
        result.depth = depth;
        result.axis = -r1[0];
        if (d1.x > 0)
        {
            result.axis = -result.axis;
        }
        if (depth > 0)
        {
            return result;
        }
    }

    // test b1 y axis
    depth = std::abs(d1.y) - e1.y - e01.y;
    if (depth > result.depth)
    {
        result.depth = depth;
        result.axis = -r1[1];
        if (d1.y > 0)
        {
            result.axis = -result.axis;
        }
    }

    return result;
}

struct ColliderInfo
{
    glm::vec2 halfExtents;
    glm::vec2 aabbMin, aabbMax;
};

class CollisionWorld : public ComponentManager<ColliderInfo>
{
    std::vector<CollisionRecord> collisionRecords;
    std::vector<uint32_t> sortIndices;
    std::vector<uint32_t> intervals;

public:
    void update(SceneGraph& sceneGraph)
    {
        for (auto index : indices())
        {
            const auto& worldTransform = sceneGraph.getWorldTransform(index);
            auto m = rotationMat2(worldTransform.rotation);
            m = glm::mat2(glm::abs(m[0]), glm::abs(m[1]));
            auto& collider = get(index);
            collider.aabbMax = m * collider.halfExtents;
            collider.aabbMin = worldTransform.position - collider.aabbMax;
            collider.aabbMax = worldTransform.position + collider.aabbMax;
        }

        sortIndices.assign(indices().begin(), indices().end());
        std::sort(sortIndices.begin(), sortIndices.end(), [this] (auto index0, auto index1)
                {
                    return get(index0).aabbMin.x < get(index1).aabbMin.x;
                });

        collisionRecords.clear();
        intervals.clear();
        for (auto index0 : sortIndices)
        {
            // remove inactive intervals
            uint32_t currentInterval = 0;
            const auto& collider0 = get(index0);
            for (uint32_t i = 0; i < intervals.size(); ++i)
            {
                auto index1 = intervals[i];
                const auto& collider1 = get(index1);
                if (collider1.aabbMax.x < collider0.aabbMin.x)
                {
                    continue;
                }
                intervals[currentInterval++] = index1;
            }
            intervals.resize(currentInterval);

            // test aabbs and add pairs
            for (auto index1 : intervals)
            {
                const auto& collider1 = get(index1);
                if (collider0.aabbMax.x >= collider1.aabbMin.x && collider0.aabbMax.y >= collider1.aabbMin.y && collider1.aabbMax.x > collider0.aabbMin.x && collider1.aabbMax.y >= collider0.aabbMin.y)
                {
                    auto result = collideBoxes(sceneGraph.getWorldTransform(index0), collider0.halfExtents, sceneGraph.getWorldTransform(index1), collider1.halfExtents);
                    if (result.depth < 0)
                    {
                        result.index0 = index0;
                        result.index1 = index1;
                        collisionRecords.push_back(result);
                    }
                }
            }

            intervals.push_back(index0);
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

struct CharacterDescription
{
    glm::vec4 color { 1.0 };
    glm::vec2 frontShoulderPosition { .28125, 0.875 };
    glm::vec2 backShoulderPosition { -0.0625, 0.875 };
    glm::vec2 armDrawSize { 0.3125, 1.0 };
    glm::vec2 bodyDrawSize { 1.0, 2.0 };
    glm::vec2 baseSize { 1.0, 1.0 };
    glm::vec2 bodyHurtboxPosition;
    glm::vec2 bodyHurtboxSize;
    float bodyHurtboxMultiplier;
    glm::vec2 headHurtboxPosition;
    glm::vec2 headHurtboxSize;
    float headHurtboxMultiplier;
    glm::vec2 armHurtboxSize;
    float armHurtboxMultiplier;
    float armLength = 0.75f;
    GLuint characterTexture;
    GLuint armTexture;
    float mass;
    float maxHealth;
};

struct Scene
{
    SceneGraph sceneGraph;
    CollisionWorld collisionWorld;
    ComponentManager<Enemy> enemies;
    ComponentManager<Character> characters;
    ComponentManager<Weapon> weapons;
    ComponentManager<Health> healthComponents;
    ComponentManager<Hurtbox> hurtboxes;
    ComponentManager<DrawInstance> drawInstances;
    ComponentManager<Dynamic> dynamics;
    ComponentManager<Player> players;
    EntityManager entityManager;
    std::vector<uint32_t> died;

    Scene()
    {
        entityManager.addComponentManager(sceneGraph);
        entityManager.addComponentManager(collisionWorld);
        entityManager.addComponentManager(enemies);
        entityManager.addComponentManager(drawInstances);
        entityManager.addComponentManager(characters);
        entityManager.addComponentManager(weapons);
        entityManager.addComponentManager(healthComponents);
        entityManager.addComponentManager(dynamics);
        entityManager.addComponentManager(hurtboxes);
        entityManager.addComponentManager(players);
    }

    void setCharacterFlipHorizontal(uint32_t index, bool flipHorizontal)
    {
        auto& character = characters.get(index);
        if (character.flipHorizontal != flipHorizontal)
        {
            for (auto spriteIndex : character.spriteIndices)
            {
                auto& instance = drawInstances.get(spriteIndex);
                instance.flipHorizontal = !instance.flipHorizontal;
            }
            float prevFrontShoulderRotation = sceneGraph.getLocalTransform(character.frontShoulder).rotation;
            sceneGraph.setPosition(character.frontShoulder, glm::vec2(-1, 1) * sceneGraph.getLocalTransform(character.frontShoulder).position);
            sceneGraph.setRotation(character.frontShoulder, -sceneGraph.getLocalTransform(character.backShoulder).rotation);
            sceneGraph.setPosition(character.backShoulder, glm::vec2(-1, 1) * sceneGraph.getLocalTransform(character.backShoulder).position);
            sceneGraph.setRotation(character.backShoulder, -prevFrontShoulderRotation);
            if (character.weapon)
            {
                auto& weapon = weapons.get(character.weapon);
                if (flipHorizontal)
                {
                    weapon.armPivot = character.backShoulder;
                    sceneGraph.setParent(character.weapon, character.backHand);
                }
                else
                {
                    weapon.armPivot = character.frontShoulder;
                    sceneGraph.setParent(character.weapon, character.frontHand);
                }
                sceneGraph.setRotation(character.weapon, -sceneGraph.getLocalTransform(character.weapon).rotation);
                weapon.flipHorizontal = flipHorizontal;
            }
            character.flipHorizontal = flipHorizontal;
        }
    }

    void addHealthComponent(uint32_t index, float maxHealth)
    {
        healthComponents.create(index);
        auto& health = healthComponents.get(index);
        health.healthyColor = { 0.0, 1.0, 0.0, 1.0 };
        health.damagedColor = { 1.0, 0.0, 0.0, 1.0 };
        health.invincibleColor = { 1.0, 1.0, 0.0 , 1.0};
        health.max = maxHealth;
        health.value = maxHealth;
        health.state = Health::State::Normal;
        health.healthBar = entityManager.create();
        sceneGraph.create(health.healthBar, index);
        sceneGraph.setPosition(health.healthBar, { 0, -1 });
        drawInstances.create(health.healthBar);
        auto& instance = drawInstances.get(health.healthBar);
        instance.color = health.healthyColor;
        instance.size = { 1.0, 0.25f };
    }

    uint32_t createSprite(uint32_t parent, const glm::vec2& position, const glm::vec2& size, const glm::vec4& color, GLuint texture, bool flipHorizontal = false)
    {
        auto index = entityManager.create();
        sceneGraph.create(index, parent);
        sceneGraph.setPosition(index, position);
        drawInstances.create(index);
        auto& instance = drawInstances.get(index);
        instance.color = color;
        instance.size = size;
        instance.texture = texture;
        instance.flipHorizontal = flipHorizontal;
        return index;
    }

    uint32_t createHurtbox(uint32_t parent, uint32_t owner, const glm::vec2& position, const glm::vec2& size, float multiplier)
    {
        auto index = entityManager.create();
        sceneGraph.create(index, parent);
        sceneGraph.setPosition(index, position);
        hurtboxes.create(index);
        auto& hurtbox = hurtboxes.get(index);
        hurtbox.multiplier = multiplier;
        hurtbox.owner = owner;
        collisionWorld.create(index);
        auto& collider = collisionWorld.get(index);
        collider.halfExtents = 0.5f * size;
        return index;
    }

    uint32_t createWeapon(uint32_t owner, const WeaponDescription& description)
    {
        auto& character = characters.get(owner);
        auto index = entityManager.create();
        character.weapon = index;
        weapons.create(index);
        Weapon& weapon = weapons.get(index);
        weapon.state = Weapon::State::Idle;
        weapon.owner = owner;
        weapon.armPivot = character.frontShoulder;
        weapon.damage = description.damage;
        weapon.animation = description.animation;
        drawInstances.create(index);
        auto& instance = drawInstances.get(index);
        instance.color = description.color;
        instance.size = description.size;
        instance.texture = description.texture;
        sceneGraph.create(index, character.frontHand);
        sceneGraph.setPosition(index, { 0, 0.5f * description.size.y });
        collisionWorld.create(index);
        auto& collider = collisionWorld.get(index);
        collider.halfExtents = 0.5f * description.size;
        return index;
    }

    uint32_t createCharacter(const CharacterDescription& description)
    {
        uint32_t index = entityManager.create();
        sceneGraph.create(index);

        collisionWorld.create(index);
        auto& collider = collisionWorld.get(index);
        collider.halfExtents = 0.5f * description.baseSize;

        dynamics.create(index);
        auto& body = dynamics.get(index);
        body.mass = description.mass;
        body.damping = 0.1f;

        addHealthComponent(index, description.maxHealth);

        characters.create(index);
        auto& character = characters.get(index);

        character.frontShoulder = entityManager.create();
        sceneGraph.create(character.frontShoulder, index);
        sceneGraph.setPosition(character.frontShoulder, description.frontShoulderPosition);
        sceneGraph.setDepth(character.frontShoulder, 0.1f);

        character.frontHand = entityManager.create();
        sceneGraph.create(character.frontHand, character.frontShoulder);
        sceneGraph.setPosition(character.frontHand, { 0, -description.armLength });
        sceneGraph.setRotation(character.frontHand, M_PI_2f);

        character.backShoulder = entityManager.create();
        sceneGraph.create(character.backShoulder, index);
        sceneGraph.setPosition(character.backShoulder, description.backShoulderPosition);
        sceneGraph.setDepth(character.backShoulder, -0.1f);

        character.backHand = entityManager.create();
        sceneGraph.create(character.backHand, character.backShoulder);
        sceneGraph.setPosition(character.backHand, { 0, -description.armLength });
        sceneGraph.setRotation(character.backHand, -M_PI_2f);

        character.spriteIndices.push_back(createSprite(index, { 0, 0.5f * (description.bodyDrawSize.y - description.baseSize.y) }, description.bodyDrawSize, description.color, description.characterTexture));
        character.spriteIndices.push_back(createSprite(character.frontShoulder, { 0, -0.5f * description.armLength }, description.armDrawSize, description.color, description.armTexture));
        character.spriteIndices.push_back(createSprite(character.backShoulder, { 0, -0.5f * description.armLength }, description.armDrawSize, description.color, description.armTexture, true));

        createHurtbox(index, index, description.bodyHurtboxPosition, description.bodyHurtboxSize, description.bodyHurtboxMultiplier);
        createHurtbox(index, index, description.headHurtboxPosition, description.headHurtboxSize, description.headHurtboxMultiplier);
        createHurtbox(character.frontShoulder, index, { 0, -0.5f * description.armLength }, description.armHurtboxSize, description.armHurtboxMultiplier);
        createHurtbox(character.backShoulder, index, { 0, -0.5f * description.armLength }, description.armHurtboxSize, description.armHurtboxMultiplier);

        return index;
    }

    void updateWeapons(uint64_t timerValue)
    {
        for (auto index : weapons.indices())
        {
            auto& weapon = weapons.get(index);
            float stateTime = static_cast<float>(static_cast<double>(timerValue - weapon.stateTimer) / static_cast<double>(glfwGetTimerFrequency()));
            if (weapon.state == Weapon::State::Swing)
            {
                uint32_t poseIndex = 1;
                const auto& animation = *weapon.animation;
                for (; poseIndex < animation.poseTimes.size() && stateTime > animation.poseTimes[poseIndex]; ++poseIndex);
                if (poseIndex < animation.poseTimes.size())
                {
                    float angle0 = animation.poseAngles[poseIndex - 1];
                    float angleSpan = animation.poseAngles[poseIndex] - angle0;
                    float time0 = animation.poseTimes[poseIndex - 1];
                    float timeSpan = animation.poseTimes[poseIndex] - time0;
                    float angle = angle0 + angleSpan * (stateTime - time0) / timeSpan;
                    weapon.sharp = animation.poseSharp[poseIndex - 1];

                    sceneGraph.setRotation(weapon.armPivot, weapon.flipHorizontal ? -angle : angle);
                }
                else
                {
                    weapon.sharp = false;
                    weapon.state = Weapon::State::Idle;
                }
            }
        }
    }

    void updatePlayer(GLFWwindow* window, const glm::vec2& cursorScenePosition, uint64_t timerValue, float dt)
    {
        for (auto index : players.indices())
        {
            const auto& player = players.get(index);
            glm::vec2 playerToCursor = cursorScenePosition - sceneGraph.getWorldTransform(index).position;
            if (playerToCursor.x > 0.2f)
            {
                setCharacterFlipHorizontal(index, true);
            }
            else if (playerToCursor.x < -0.2f)
            {
                setCharacterFlipHorizontal(index, false);
            }

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
                targetVelocity = glm::normalize(moveInput) * player.speed;
            }
            updateVelocity(dynamics.get(index), targetVelocity, player.acceleration, dt);

            if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT))
            {
                auto& weapon = weapons.get(characters.get(index).weapon);
                if (weapon.state == Weapon::State::Idle)
                {
                    weapon.state = Weapon::State::Swing;
                    weapon.stateTimer = timerValue;
                }
            }
        }
    }

    void updateEnemyAI(uint64_t timerValue, float dt)
    {
        for (auto index : enemies.indices())
        {
            auto& enemy = enemies.get(index);
            auto& character = characters.get(index);

            uint32_t target = 0;
            glm::vec2 toPlayer(0);
            float toPlayerDistance = 0;
            for (auto playerIndex : players.indices())
            { 
                glm::vec2 to = sceneGraph.getWorldTransform(playerIndex).position - sceneGraph.getWorldTransform(index).position;
                float tod2 = glm::dot(to, to);
                if (!target || tod2 < toPlayerDistance)
                {
                    target = playerIndex;
                    toPlayerDistance = tod2;
                    toPlayer = to;
                }
            }

            enemy.moveInput = glm::vec2(0);
            if (target != 0 && toPlayerDistance < 25.0f)
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

                    if ((toPlayer.x > 0) != enemy.wantToFace)
                    {
                        enemy.wantToFace = (toPlayer.x > 0);
                        enemy.turnDelayTimeAccumulator = 0;
                    }
                    else if (enemy.turnDelayTimeAccumulator >= enemy.turnDelayTime)
                    {
                        setCharacterFlipHorizontal(index, enemy.wantToFace);
                    }
                    else if (enemy.wantToFace != character.flipHorizontal)
                    {
                        enemy.turnDelayTimeAccumulator += dt;
                    }

                    if (glm::dot(toPlayer, toPlayer) <= 1.0)
                    {
                        auto& weapon = weapons.get(character.weapon);
                        if (weapon.state == Weapon::State::Idle && (timerValue - weapon.stateTimer) >= glfwGetTimerFrequency() * enemy.attackRechargeTime)
                        {
                            weapon.state = Weapon::State::Swing;
                            weapon.stateTimer = timerValue;
                        }
                    }
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
    }

    void updateDynamics(float dt)
    {
        for (auto index : dynamics.indices())
        {
            auto& body = dynamics.get(index);
            sceneGraph.setPosition(index, sceneGraph.getLocalTransform(index).position + body.velocity * dt);
            body.velocity -= body.damping * body.velocity * dt;
        }
    }

    void updateCollision()
    {
        collisionWorld.update(sceneGraph);

        for (const auto& record : collisionWorld.getCollisionRecords())
        {
            // dynamics stuff
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
                    body0.velocity += impulse / body0.mass;
                    body1.velocity -= impulse / body1.mass;
                }
            }

            // health component stuff
            for (int i = 0; i < 2; ++i)
            {
                uint32_t active = (i == 0) ? record.index0 : record.index1;
                uint32_t other = (i == 0) ? record.index1 : record.index0;

                if (weapons.has(active))
                {
                    const auto& weapon = weapons.get(active);
                    if (weapon.sharp && hurtboxes.has(other))
                    {
                        auto& hurtbox = hurtboxes.get(other);
                        auto& health = healthComponents.get(hurtbox.owner);
                        if (hurtbox.owner != weapon.owner && health.state != Health::State::Invincible)
                        {
                            health.value -= hurtbox.multiplier * weapon.damage;
                            health.takingDamage = true;
                        }
                    }
                }
            }
        }
    }

    void updateHealth(uint64_t timerValue)
    {
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
            auto& instance = drawInstances.get(health.healthBar);
            if (health.state == Health::State::Invincible)
            {
                instance.color = health.invincibleColor;
            }
            else
            {
                instance.color = glm::mix(health.damagedColor, health.healthyColor, health.value / health.max);
            }
            instance.size.x = health.value / health.max;
        }

        for (auto index : died)
        {
            sceneGraph.destroyHierarchy(entityManager, index);
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

    Renderer renderer;
    Scene scene;

    float cameraViewHeight = 20.0f;
    glm::vec2 cameraPosition(0, 0);

    std::vector<GLuint> textures;
    auto characterTexture = textures.emplace_back(loadTexture("textures/character.png"));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    auto armTexture = textures.emplace_back(loadTexture("textures/arm.png"));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    
    CharacterDescription playerBodyDescription {};
    playerBodyDescription.color = { 1.0, 1.0, 1.0, 1.0 };
    playerBodyDescription.frontShoulderPosition = { .28125, 0.875 };
    playerBodyDescription.backShoulderPosition = { -0.0625, 0.875 };
    playerBodyDescription.armDrawSize = { 0.3125, 1.0 };
    playerBodyDescription.bodyDrawSize = { 1.0, 2.0 };
    playerBodyDescription.baseSize = { 1.0, 1.0 };
    playerBodyDescription.armLength = 0.75f;
    playerBodyDescription.bodyHurtboxPosition = { -0.03125, 0.5 };
    playerBodyDescription.bodyHurtboxSize = { 0.5, 1.0 };
    playerBodyDescription.bodyHurtboxMultiplier = 1.0f;
    playerBodyDescription.headHurtboxPosition = { -0.375, 1.1875};
    playerBodyDescription.headHurtboxSize = { 0.44, 0.47 };
    playerBodyDescription.headHurtboxMultiplier = 1.5f;
    playerBodyDescription.armHurtboxSize = { 0.16, 0.75 };
    playerBodyDescription.armHurtboxMultiplier = 0.8f;
    playerBodyDescription.characterTexture = characterTexture;
    playerBodyDescription.armTexture = armTexture;
    playerBodyDescription.mass = 15.0f;
    playerBodyDescription.maxHealth = 20.0f;

    CharacterDescription zombieBodyDescription = playerBodyDescription;
    zombieBodyDescription.color = { 0.5, 1.0, 0.7, 1.0 };
    zombieBodyDescription.mass = 10.0f;
    zombieBodyDescription.maxHealth = 10.0f;

    WeaponAnimation weaponAnimation {};
    weaponAnimation.poseAngles = { 0.0f, -M_PI_2f, M_PI_4f, 0.0f };
    weaponAnimation.poseTimes = { 0.0f, 0.1f, 0.2f, 0.45f };
    weaponAnimation.poseSharp = { false, true, false, false };

    WeaponDescription weaponDescription {};
    weaponDescription.animation = &weaponAnimation;
    weaponDescription.damage = 2.0f;
    weaponDescription.size = { 0.1f, 0.5f };
    weaponDescription.color = { 0.8, 0.8, 0.8, 1.0 };
    weaponDescription.texture = 0;

    WeaponAnimation zombieWeaponAnimation {};
    zombieWeaponAnimation.poseAngles = { -M_PI_2f, -M_PI_2f - M_PI_4f, -M_PI_2f - M_PI_4f, -M_PI_2f + M_PI_4f, -M_PI_2f };
    zombieWeaponAnimation.poseTimes = { 0.0f, 0.2f, 0.5f, 0.6f, 0.8f };
    zombieWeaponAnimation.poseSharp = { false, true, true, false, false };

    WeaponDescription zombieWeaponDescription {};
    zombieWeaponDescription.animation = &zombieWeaponAnimation;
    zombieWeaponDescription.color = { 0.0f, 0.0f, 0.0f, 0.0f };
    zombieWeaponDescription.damage = 0.8f;
    zombieWeaponDescription.size = { 0.15f, 0.15f };

    for (auto i = 0; i < 100; ++i)
    {
        auto index = scene.createCharacter(zombieBodyDescription);
        scene.sceneGraph.setPosition(index, glm::linearRand(glm::vec2(-20), glm::vec2(20)));

        scene.enemies.create(index);
        Enemy& enemy = scene.enemies.get(index);
        enemy.speed = 2.0f;
        enemy.moveInput = glm::vec2(0.0f);
        enemy.state = Enemy::State::Idle;
        enemy.attackRechargeTime = 0.5f;

        scene.createWeapon(index, zombieWeaponDescription);
    }

    {
        auto index = scene.createCharacter(playerBodyDescription);
        scene.players.create(index);
        auto& player = scene.players.get(index);
        player.acceleration = 25.0f;
        player.speed = 5.0f;
        scene.createWeapon(index, weaponDescription);
    }

    {
        uint32_t index = scene.createSprite(0, { 0, -5 }, { 3, 2 }, { 0.8, 0.3, 0.2, 1.0 }, 0, false);
        scene.collisionWorld.create(index);
        auto& collider = scene.collisionWorld.get(index);
        collider.halfExtents = { 1.5, 1.0 };
        scene.dynamics.create(index);
    }

    uint64_t timerValue = glfwGetTimerValue();
    uint64_t fpsTimer = timerValue;
    uint32_t frames = 0;

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

        if (timerValue - fpsTimer >= glfwGetTimerFrequency())
        {
            double fps = static_cast<double>(frames * glfwGetTimerFrequency()) / static_cast<double>(timerValue - fpsTimer);
            std::string windowTitle = "FPS: " + std::to_string(static_cast<int>(fps + 0.5));
            glfwSetWindowTitle(window, windowTitle.c_str());
            frames = 0;
            fpsTimer = timerValue;
        }
        ++frames;

        int windowWidth, windowHeight;
        glfwGetFramebufferSize(window, &windowWidth, &windowHeight);

        glm::mat4 pixelOrtho = glm::ortho<float>(0, windowWidth, 0, windowHeight);

        float aspectRatio = static_cast<float>(windowWidth) / static_cast<float>(windowHeight);
        glm::vec2 cameraViewHalfExtents(0.5f * aspectRatio * cameraViewHeight, 0.5f * cameraViewHeight);
        glm::mat4 cameraMatrix = glm::ortho(cameraPosition.x - cameraViewHalfExtents.x, cameraPosition.x + cameraViewHalfExtents.x, cameraPosition.y - cameraViewHalfExtents.y, cameraPosition.y + cameraViewHalfExtents.y);

        glm::mat4 pixelToSceneMatrix = glm::inverse(cameraMatrix) * pixelOrtho;

        double cursorX, cursorY;
        glfwGetCursorPos(window, &cursorX, &cursorY);
        glm::vec2 cursorScenePosition = glm::vec2(pixelToSceneMatrix * glm::vec4(cursorX, windowHeight - cursorY, 0, 1));
        
        scene.updatePlayer(window, cursorScenePosition, timerValue, dt);
        scene.updateEnemyAI(timerValue, dt);
        scene.updateWeapons(timerValue);
        scene.updateDynamics(dt);
        scene.updateCollision();
        scene.updateHealth(timerValue);

        renderer.prepareRender(scene.sceneGraph, scene.drawInstances, cameraMatrix);

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
