#pragma once

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

class SceneGraph;
template<typename T> class ComponentManager;

struct CollisionRecord
{
    uint32_t index0, index1;
    float depth;
    glm::vec2 axis;
};

using CollisionCallback = void (*) (uint32_t, uint32_t, const CollisionRecord&, void*);

struct Collider
{
    glm::vec2 halfExtents;
    glm::vec2 aabbMin, aabbMax;
    CollisionCallback callback = nullptr;
    void* callbackData = nullptr;
};

struct Dynamic
{
    float mass = 0.0f;
    float damping = 0.0f;
    glm::vec2 velocity = { 0.0f, 0.0f };
};

class PhysicsWorld
{
    SceneGraph& sceneGraph;
    ComponentManager<Collider>& colliders;
    ComponentManager<Dynamic>& dynamics;

    std::vector<CollisionRecord> collisionRecords;
    std::vector<uint32_t> sortIndices;
    std::vector<uint32_t> intervals;

public:
    PhysicsWorld(SceneGraph& sceneGraph, ComponentManager<Collider>& colliders, ComponentManager<Dynamic>& dynamics);

    void update(float dt);

    const std::vector<CollisionRecord>& getCollisionRecords() const;
};
