#include "physics_world.hpp"

#include <algorithm>

#include "scene_graph.hpp"

static glm::mat2 rotationMat2(float rotation)
{
    float cosAngle = std::cos(rotation);
    float sinAngle = std::sin(rotation);
    return glm::mat2(cosAngle, sinAngle, -sinAngle, cosAngle);
}

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

PhysicsWorld::PhysicsWorld(SceneGraph& sceneGraph, ComponentManager<Collider>& colliders, ComponentManager<Dynamic>& dynamics) :
    sceneGraph(sceneGraph),
    colliders(colliders),
    dynamics(dynamics)
{
}

void PhysicsWorld::update(float dt)
{
    for (auto index : dynamics.indices())
    {
        auto& body = dynamics.get(index);
        sceneGraph.setPosition(index, sceneGraph.getLocalTransform(index).position + body.velocity * dt);
        body.velocity -= body.damping * body.velocity * dt;
    }

    const auto& colliderIndices = colliders.indices();
    for (auto index : colliderIndices)
    {
        const auto& worldTransform = sceneGraph.getWorldTransform(index);
        auto m = rotationMat2(worldTransform.rotation);
        m = glm::mat2(glm::abs(m[0]), glm::abs(m[1]));
        auto& collider = colliders.get(index);
        collider.aabbMax = m * collider.halfExtents;
        collider.aabbMin = worldTransform.position - collider.aabbMax;
        collider.aabbMax = worldTransform.position + collider.aabbMax;
    }

    sortIndices.assign(colliderIndices.begin(), colliderIndices.end());
    std::sort(sortIndices.begin(), sortIndices.end(),
        [this] (auto index0, auto index1)
        {
            return colliders.get(index0).aabbMin.x < colliders.get(index1).aabbMin.x;
        });

    collisionRecords.clear();
    intervals.clear();
    for (auto index0 : sortIndices)
    {
        // remove inactive intervals
        uint32_t currentInterval = 0;
        const auto& collider0 = colliders.get(index0);
        for (uint32_t i = 0; i < intervals.size(); ++i)
        {
            auto index1 = intervals[i];
            const auto& collider1 = colliders.get(index1);
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
            const auto& collider1 = colliders.get(index1);
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

    for (const auto& record : collisionRecords)
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
                body0.velocity += impulse / body0.mass;
                body1.velocity -= impulse / body1.mass;
            }
        }

        for (int i = 0; i < 2; ++i)
        {
            uint32_t active = (i == 0) ? record.index0 : record.index1;
            uint32_t other = (i == 0) ? record.index1 : record.index0;
            const auto& collider = colliders.get(active);
            if (collider.callback)
            {
                collider.callback(active, other, record, collider.callbackData);
            }
        }
    }
}

const std::vector<CollisionRecord>& PhysicsWorld::getCollisionRecords() const
{
    return collisionRecords;
}
