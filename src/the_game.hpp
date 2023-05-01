#pragma once

#include "game.hpp"
#include "ecs.hpp"
#include "scene_graph.hpp"
#include "physics_world.hpp"
#include "renderer.hpp"

using GenericCallback = void (*) (uint32_t, void*);
using ConditionCallback = bool (*) (uint32_t, void*);

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
    float stateTimer;
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
    uint32_t delivery = 0;
    uint32_t target = 0;
    uint32_t arrow = 0;
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
    float stateTimer;
    float invincibleTime = 1.0f;
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
    bool triggered = false;
    int key;
    GenericCallback callback = nullptr;
    ConditionCallback condition = nullptr;
};

struct UIElement
{
    GenericCallback onClick;
};

struct Delivery
{
    uint32_t address;
    float value;
};

struct DeliveryAddress
{
};

struct Depot
{
};

struct Arrow
{
    uint32_t source;
    uint32_t target;
};

class TheGame final : public Game
{
    SceneGraph sceneGraph;
    ComponentManager<Arrow> arrows;
    ComponentManager<Character> characters;
    ComponentManager<Collider> colliders;
    ComponentManager<Delivery> deliveries;
    ComponentManager<DeliveryAddress> addresses;
    ComponentManager<Depot> depots;
    ComponentManager<DrawInstance> drawInstances;
    ComponentManager<Dynamic> dynamics;
    ComponentManager<Enemy> enemies;
    ComponentManager<Health> healthComponents;
    ComponentManager<Hurtbox> hurtboxes;
    ComponentManager<Player> players;
    ComponentManager<TextInstance> textInstances;
    ComponentManager<Trigger> triggers;
    ComponentManager<Weapon> weapons;
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
    GLuint arrowTexture;

public:
    TheGame();
    ~TheGame();

    void update(GLFWwindow* window) override;
    void draw() override;

    void addHealthComponent(uint32_t index, float maxHealth, GenericCallback onDied = nullptr);
    uint32_t createSprite(uint32_t parent, const glm::vec2& position, const glm::vec2& size, const glm::vec4& color, GLuint texture, bool flipHorizontal = false, float heightForDepth = 0);
    uint32_t createHurtbox(uint32_t parent, uint32_t owner, const glm::vec2& position, const glm::vec2& size, float multiplier);
    uint32_t createWeapon(uint32_t owner, const WeaponDescription& description);
    uint32_t createCharacter(const glm::vec2& position, const CharacterDescription& description);
    uint32_t createTrigger(uint32_t parent, const glm::vec2& position, const glm::vec2& size, int key, GenericCallback callback, ConditionCallback condition = nullptr);
    uint32_t createPlayer(const glm::vec2& position);
    uint32_t createZombie(const glm::vec2& position);
    uint32_t createOverlay(const glm::vec2& position, const glm::vec2& size, GLuint texture);

    void updateWeapons(float dt);
    void updatePlayer(GLFWwindow* window, const glm::vec2& cursorScenePosition, float dt);
    void updateEnemyAI(float dt);
    void updateHealth(float dt);

    void setCharacterFlipHorizontal(uint32_t index, bool flipHorizontal);
    void onWeaponCollision(uint32_t index, uint32_t other, const CollisionRecord& collisionRecord);
    void onTriggerCollision(uint32_t index, uint32_t other, const CollisionRecord& collisionRecord);
    void onTriggerDepotOverlay();
    void onPlayerDied();
    bool hasDeliveryForAddress(uint32_t address);
    void completeDelivery();
};
