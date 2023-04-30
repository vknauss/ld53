#pragma once

#include "game.hpp"
#include "ecs.hpp"
#include "scene_graph.hpp"
#include "physics_world.hpp"
#include "renderer.hpp"

using GenericCallback = void (*) (void*, uint32_t);

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
    GenericCallback onDied;
};

struct Hurtbox
{
    float multiplier = 1.0f;
    uint32_t owner;
};

struct Trigger
{
    bool active = false;
    int key;
    GenericCallback callback;
};

class TheGame final : public Game
{
    SceneGraph sceneGraph;
    ComponentManager<Enemy> enemies;
    ComponentManager<Character> characters;
    ComponentManager<Weapon> weapons;
    ComponentManager<Health> healthComponents;
    ComponentManager<Hurtbox> hurtboxes;
    ComponentManager<DrawInstance> drawInstances;
    ComponentManager<Collider> colliders;
    ComponentManager<Dynamic> dynamics;
    ComponentManager<Player> players;
    ComponentManager<Trigger> triggers;
    EntityManager entityManager;
    Renderer renderer;
    PhysicsWorld physicsWorld;
    std::vector<GLuint> textures;
    CharacterDescription playerBodyDescription;
    CharacterDescription zombieBodyDescription;
    WeaponAnimation weaponAnimation;
    WeaponAnimation zombieWeaponAnimation;
    WeaponDescription weaponDescription;
    WeaponDescription zombieWeaponDescription;
    std::vector<uint32_t> died;
    bool playerDied = false;
    glm::vec2 cameraPosition;
    float cameraViewHeight;
    float uiViewHeight;
    glm::mat4 cameraMatrix;
    glm::mat4 uiCameraMatrix;
    int windowWidth;
    int windowHeight;
    uint64_t timerValue;
    uint64_t fpsTimer;
    uint32_t frames;

public:
    TheGame();
    ~TheGame();

    void update(GLFWwindow* window) override;
    void draw() override;

    void addHealthComponent(uint32_t index, float maxHealth, GenericCallback onDied = nullptr);
    uint32_t createSprite(uint32_t parent, const glm::vec2& position, const glm::vec2& size, const glm::vec4& color, GLuint texture, bool flipHorizontal = false, float heightForDepth = 0);
    uint32_t createHurtbox(uint32_t parent, uint32_t owner, const glm::vec2& position, const glm::vec2& size, float multiplier);
    uint32_t createWeapon(uint32_t owner, const WeaponDescription& description);
    uint32_t createCharacter(const CharacterDescription& description);
    uint32_t createTrigger(uint32_t parent, const glm::vec2& position, const glm::vec2& size, int key, GenericCallback callback);

    void updateWeapons(uint64_t timerValue);
    void updatePlayer(GLFWwindow* window, const glm::vec2& cursorScenePosition, uint64_t timerValue, float dt);
    void updateEnemyAI(uint64_t timerValue, float dt);
    void updateHealth(uint64_t timerValue);

    void setCharacterFlipHorizontal(uint32_t index, bool flipHorizontal);
    void onWeaponCollision(uint32_t index, uint32_t other, const CollisionRecord& collisionRecord);
    void onTriggerCollision(uint32_t index, uint32_t other, const CollisionRecord& collisionRecord);
};
