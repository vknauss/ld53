#include "the_game.hpp"

#include <string>
#include <GLFW/glfw3.h>
#include <glm/gtc/random.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "opengl_utils.hpp"

#define PIXELS_PER_WORLD_UNIT 32

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

static glm::mat4 computeViewMatrix(int windowWidth, int windowHeight, float pixelsPerWorldUnit, float targetViewableHeight, const glm::vec2& viewCenter)
{
    float baseWorldUnitsPerHeight = static_cast<float>(windowHeight) / pixelsPerWorldUnit;
    float pixelScale = std::ceil(baseWorldUnitsPerHeight / targetViewableHeight);
    float actualViewHeight = baseWorldUnitsPerHeight / pixelScale;
    float aspectRatio = static_cast<float>(windowWidth) / static_cast<float>(windowHeight);
    glm::vec2 viewHalfExtents(0.5f * aspectRatio * actualViewHeight, 0.5f * actualViewHeight);

    return glm::ortho(viewCenter.x - viewHalfExtents.x, viewCenter.x + viewHalfExtents.x, viewCenter.y - viewHalfExtents.y, viewCenter.y + viewHalfExtents.y);
}

static void weaponCollisionCallback(uint32_t index, uint32_t other, const CollisionRecord& record, void* data)
{
    static_cast<TheGame*>(data)->onWeaponCollision(index, other, record);
}

static void triggerCollisionCallback(uint32_t index, uint32_t other, const CollisionRecord& record, void* data)
{
    static_cast<TheGame*>(data)->onTriggerCollision(index, other, record);
}

Game* createGame()
{
    return new TheGame();
}

TheGame::TheGame() :
    physicsWorld(sceneGraph, colliders, dynamics),
    cameraPosition(0, 0),
    cameraViewHeight(20.0f),
    uiViewHeight(10.0f),
    timerValue(glfwGetTimerValue()),
    fpsTimer(timerValue),
    frames(0)
{
    entityManager.addComponentManager(sceneGraph);
    entityManager.addComponentManager(colliders);
    entityManager.addComponentManager(enemies);
    entityManager.addComponentManager(drawInstances);
    entityManager.addComponentManager(characters);
    entityManager.addComponentManager(weapons);
    entityManager.addComponentManager(healthComponents);
    entityManager.addComponentManager(dynamics);
    entityManager.addComponentManager(hurtboxes);
    entityManager.addComponentManager(players);

    auto characterTexture = textures.emplace_back(loadTexture("textures/character.png"));
    auto armTexture = textures.emplace_back(loadTexture("textures/arm.png"));
    auto houseTexture = textures.emplace_back(loadTexture("textures/house.png"));
    auto intersectionTexture = textures.emplace_back(loadTexture("textures/intersection.png"));
    auto roadHorizontalTexture = textures.emplace_back(loadTexture("textures/road_horizontal.png"));
    auto roadVerticalTexture = textures.emplace_back(loadTexture("textures/road_vertical.png"));
    auto depotTexture = textures.emplace_back(loadTexture("textures/depot.png"));

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

    weaponAnimation = {};
    weaponAnimation.poseAngles = { 0.0f, -M_PI_2f, M_PI_4f, 0.0f };
    weaponAnimation.poseTimes = { 0.0f, 0.1f, 0.2f, 0.45f };
    weaponAnimation.poseSharp = { false, true, false, false };

    weaponDescription  = {};
    weaponDescription.animation = &weaponAnimation;
    weaponDescription.damage = 2.0f;
    weaponDescription.size = { 0.1f, 0.5f };
    weaponDescription.color = { 0.8, 0.8, 0.8, 1.0 };
    weaponDescription.texture = 0;

    zombieWeaponAnimation = {};
    zombieWeaponAnimation.poseAngles = { -M_PI_2f, -M_PI_2f - M_PI_4f, -M_PI_2f - M_PI_4f, -M_PI_2f + M_PI_4f, -M_PI_2f };
    zombieWeaponAnimation.poseTimes = { 0.0f, 0.2f, 0.5f, 0.6f, 0.8f };
    zombieWeaponAnimation.poseSharp = { false, false, true, false, false };

    zombieWeaponDescription = {};
    zombieWeaponDescription.animation = &zombieWeaponAnimation;
    zombieWeaponDescription.color = { 0.0f, 0.0f, 0.0f, 0.0f };
    zombieWeaponDescription.damage = 0.8f;
    zombieWeaponDescription.size = { 0.15f, 0.15f };

    // build city
    for (int i = 0; i < 4; ++i)
    {
        for (int j = 0; j < 4; ++j)
        {
            glm::vec2 offset{ (12 + 12 * 4) * i, 15 * j };
            createSprite(0, offset, { 12, 9 }, { 1, 1, 1, 1 }, intersectionTexture, false, -9);
            createSprite(0, { offset.x, offset.y + 6 }, { 6, 3 }, { 1, 1, 1, 1 }, roadVerticalTexture, false, -3);
            createSprite(0, { offset.x, offset.y + 9 }, { 6, 3 }, { 1, 1, 1, 1 }, roadVerticalTexture, false, -3);

            for (int k = 0; k < 4; ++k)
            {
                offset.x += 12;
                auto index = entityManager.create();
                sceneGraph.create(index);
                sceneGraph.setPosition(index, { offset.x, offset.y + 7.25 });
                sceneGraph.setHeightForDepth(index, 3.25f);
                colliders.create(index);
                auto& collider = colliders.get(index);
                collider.halfExtents = { 5.0, 3.25 };
                dynamics.create(index);
                createSprite(index, { 0, 1.25 }, { 12, 11 }, { 1.0, 1.0, 1.0, 1.0 }, houseTexture, false);

                createSprite(0, { offset.x - 4, offset.y }, { 4, 5 }, { 1, 1, 1, 1 }, roadHorizontalTexture, false, -5);
                createSprite(0, { offset.x, offset.y }, { 4, 5 }, { 1, 1, 1, 1 }, roadHorizontalTexture, false, -5);
                createSprite(0, { offset.x + 4, offset.y }, { 4, 5 }, { 1, 1, 1, 1 }, roadHorizontalTexture, false, -5);
            }
        }
    }

    for (auto i = 0; i < 100; ++i)
    {
        auto index = createCharacter(zombieBodyDescription);
        sceneGraph.setPosition(index, glm::linearRand(glm::vec2(-20), glm::vec2(20)));

        enemies.create(index);
        Enemy& enemy = enemies.get(index);
        enemy.speed = 2.0f;
        enemy.moveInput = glm::vec2(0.0f);
        enemy.state = Enemy::State::Idle;
        enemy.attackRechargeTime = 0.5f;

        createWeapon(index, zombieWeaponDescription);
    }

    {
        auto index = createCharacter(playerBodyDescription);
        players.create(index);
        auto& player = players.get(index);
        player.acceleration = 25.0f;
        player.speed = 5.0f;
        createWeapon(index, weaponDescription);
    }

    {
        uint32_t index = createSprite(0, { 0, -5 }, { 3, 2 }, { 0.8, 0.3, 0.2, 1.0 }, 0, false);
        colliders.create(index);
        auto& collider = colliders.get(index);
        collider.halfExtents = { 1.5, 1.0 };
        dynamics.create(index);
    }
}

TheGame::~TheGame()
{
    for (const auto& texture : textures)
    {
        glDeleteTextures(1, &texture);
    }
}


void TheGame::update(GLFWwindow* window)
{
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

    glfwGetFramebufferSize(window, &windowWidth, &windowHeight);
    glm::mat4 pixelOrtho = glm::ortho<float>(0, windowWidth, 0, windowHeight);

    cameraMatrix = computeViewMatrix(windowWidth, windowHeight, PIXELS_PER_WORLD_UNIT, cameraViewHeight, cameraPosition);
    uiCameraMatrix = computeViewMatrix(windowWidth, windowHeight, PIXELS_PER_WORLD_UNIT, uiViewHeight, { 0, 0});

    glm::mat4 pixelToSceneMatrix = glm::inverse(cameraMatrix) * pixelOrtho;

    double cursorX, cursorY;
    glfwGetCursorPos(window, &cursorX, &cursorY);
    glm::vec2 cursorScenePosition = glm::vec2(pixelToSceneMatrix * glm::vec4(cursorX, windowHeight - cursorY, 0, 1));

    for (auto index : triggers.indices())
    {
        triggers.get(index).active = false;
    }
    
    updatePlayer(window, cursorScenePosition, timerValue, dt);
    updateEnemyAI(timerValue, dt);
    updateWeapons(timerValue);
    physicsWorld.update(dt);
    updateHealth(timerValue);

    if (!playerDied)
    {
        glm::vec2 cameraToPlayer = sceneGraph.getWorldTransform(players.indices().front()).position - cameraPosition;
        cameraPosition += dt * cameraToPlayer;
    }
}

void TheGame::draw()
{
    renderer.prepareRender(sceneGraph, drawInstances,  { cameraMatrix, uiCameraMatrix });
    renderer.render(windowWidth, windowHeight, { 0.1, 0.5, 0.1, 1.0} );
}

void TheGame::setCharacterFlipHorizontal(uint32_t index, bool flipHorizontal)
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

void TheGame::addHealthComponent(uint32_t index, float maxHealth, GenericCallback onDied)
{
    healthComponents.create(index);
    auto& health = healthComponents.get(index);
    health.healthyColor = { 0.0, 1.0, 0.0, 1.0 };
    health.damagedColor = { 1.0, 0.0, 0.0, 1.0 };
    health.invincibleColor = { 1.0, 1.0, 0.0 , 1.0};
    health.max = maxHealth;
    health.value = maxHealth;
    health.state = Health::State::Normal;
    health.onDied = onDied;
    health.healthBar = entityManager.create();
    sceneGraph.create(health.healthBar, index);
    sceneGraph.setPosition(health.healthBar, { 0, -1 });
    drawInstances.create(health.healthBar);
    auto& instance = drawInstances.get(health.healthBar);
    instance.color = health.healthyColor;
    instance.size = { 1.0, 0.25f };
}

uint32_t TheGame::createSprite(uint32_t parent, const glm::vec2& position, const glm::vec2& size, const glm::vec4& color, GLuint texture, bool flipHorizontal, float heightForDepth)
{
    auto index = entityManager.create();
    sceneGraph.create(index, parent);
    sceneGraph.setPosition(index, position);
    sceneGraph.setHeightForDepth(index, heightForDepth);
    drawInstances.create(index);
    auto& instance = drawInstances.get(index);
    instance.color = color;
    instance.size = size;
    instance.texture = texture;
    instance.flipHorizontal = flipHorizontal;
    return index;
}

uint32_t TheGame::createHurtbox(uint32_t parent, uint32_t owner, const glm::vec2& position, const glm::vec2& size, float multiplier)
{
    auto index = entityManager.create();
    sceneGraph.create(index, parent);
    sceneGraph.setPosition(index, position);
    hurtboxes.create(index);
    auto& hurtbox = hurtboxes.get(index);
    hurtbox.multiplier = multiplier;
    hurtbox.owner = owner;
    colliders.create(index);
    auto& collider = colliders.get(index);
    collider.halfExtents = 0.5f * size;
    return index;
}

uint32_t TheGame::createWeapon(uint32_t owner, const WeaponDescription& description)
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
    colliders.create(index);
    auto& collider = colliders.get(index);
    collider.halfExtents = 0.5f * description.size;
    collider.callback = weaponCollisionCallback;
    collider.callbackData = this;
    return index;
}

uint32_t TheGame::createCharacter(const CharacterDescription& description)
{
    uint32_t index = entityManager.create();
    sceneGraph.create(index);

    colliders.create(index);
    auto& collider = colliders.get(index);
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

uint32_t TheGame::createTrigger(uint32_t parent, const glm::vec2& position, const glm::vec2& size, int key, GenericCallback callback)
{
    auto index = entityManager.create();
    sceneGraph.create(index, parent);
    sceneGraph.setPosition(index, position);
    colliders.create(index);
    auto& collider = colliders.get(index);
    collider.halfExtents = 0.5f * size;
    collider.callback = triggerCollisionCallback;
    collider.callbackData = this;
    triggers.create(index);
    auto& trigger = triggers.get(index);
    trigger.active = false;
    trigger.key = key;
    trigger.callback = callback;
    return index;
}

void TheGame::updateWeapons(uint64_t timerValue)
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

void TheGame::updatePlayer(GLFWwindow* window, const glm::vec2& cursorScenePosition, uint64_t timerValue, float dt)
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

void TheGame::updateEnemyAI(uint64_t timerValue, float dt)
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

void TheGame::updateHealth(uint64_t timerValue)
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
        auto& health = healthComponents.get(index);
        if (health.onDied)
        {
            health.onDied(this, index);
        }
        sceneGraph.destroyHierarchy(entityManager, index);
    }
}

void TheGame::onWeaponCollision(uint32_t index, uint32_t other, const CollisionRecord& collisionRecord)
{
    const auto& weapon = weapons.get(index);
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

void TheGame::onTriggerCollision(uint32_t index, uint32_t other, const CollisionRecord& collisionRecord)
{
    if (players.has(other))
    {
        triggers.get(index).active = true;
    }
}
