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
    float despawnTimer = 0;
    float despawnTime = 10.0f;
    float noticeDistance = 20.0f;
    float attackDistance = 2.0f;
    float despawnDistance = 25.0f;
};

struct Player
{
    float speed;
    float acceleration;
    uint32_t delivery = 0;
    uint32_t target = 0;
    uint32_t arrow = 0;
    float money = 0;
    uint32_t moneyText = 0;
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
    uint32_t text = 0;
};

struct UIElement
{
    GenericCallback onClick;
    enum class Position
    {
        Center, Left, Right, Bottom, Top, LowerLeft, UpperLeft, LowerRight, UpperRight
    };
    Position textAlign = Position::Center;
    Position anchor = Position::Center;
    glm::vec2 position = { 0.0f, 0.0f }; // base position, scene graph position will be calculated from this
};

struct Delivery
{
    uint32_t address;
    float value;
};

// empty struct can be used as a tag
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

struct CloseButton
{
    uint32_t overlay;
};

struct OverlayDeliveryItem
{
    uint32_t delivery;
};

struct DeliveryOverlay
{
    std::vector<uint32_t> deliveryItems;
};

struct DepotOverlay
{
};

struct StoreItem
{
    enum class StatBoost
    {
        HEALTH, SPEED, ATTACK
    };
    StatBoost stat;
    float boostAmount;
    float cost;
};

struct StoreOverlayItem
{
    uint32_t item;
    uint32_t costText;
    float lastCost;
};

struct Temporary
{
    float timerValue = 0;
    float duration = 1.0;
};

struct Behavior
{
    GenericCallback callback;
    float dt;
};

struct StoreOverlay
{
    std::vector<uint32_t> storeItems;
};

struct Audio;
struct Sound;

class TheGame final : public Game
{
    Audio* audio = NULL;
    Sound* bonkSound = NULL;
    SceneGraph sceneGraph;
    ComponentManager<Arrow> arrows;
    // ComponentManager<Behavior> behaviors;
    ComponentManager<Character> characters;
    ComponentManager<CloseButton> closeButtons;
    ComponentManager<Collider> colliders;
    ComponentManager<Delivery> deliveries;
    ComponentManager<DeliveryAddress> addresses;
    ComponentManager<DeliveryOverlay> deliveryOverlays;
    ComponentManager<Depot> depots;
    ComponentManager<DepotOverlay> depotOverlays;
    ComponentManager<DrawInstance> drawInstances;
    ComponentManager<Dynamic> dynamics;
    ComponentManager<Enemy> enemies;
    ComponentManager<Health> healthComponents;
    ComponentManager<Hurtbox> hurtboxes;
    ComponentManager<OverlayDeliveryItem> overlayDeliveryItems;
    ComponentManager<Player> players;
    ComponentManager<StoreItem> storeItems;
    ComponentManager<StoreOverlay> storeOverlays;
    ComponentManager<StoreOverlayItem> storeOverlayItems;
    ComponentManager<Temporary> temporaries;
    ComponentManager<TextInstance> textInstances;
    ComponentManager<Trigger> triggers;
    ComponentManager<UIElement> uiElements;
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
    glm::vec2 uiViewExtentMin;
    glm::vec2 uiViewExtentMax;
    int windowWidth;
    int windowHeight;
    uint64_t timerValue;
    uint64_t fpsTimer;
    uint32_t frames;
    GLuint arrowTexture;
    uint32_t hoveredUIElement = 0;
    float enemySpawnTimer = 0;
    GLuint closeButtonTexture;
    bool mouseButtonDown = false;
    float zombieLevel = 0.1f;
    float zombieLevelRate = 0.01;
    uint32_t zombieLevelText = 0;
    bool paused = false;
    int deliveriesCompleted = 0;
    float lifetimeMoney = 0;
    double gameTime = 0;
    bool escapeDown = false;
    bool pDown = false;
    bool fDown = false;
    bool isFullscreen = false;
    uint32_t pauseOverlay = 0;
    bool isGameOver = false;

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
    uint32_t createOverlay(const glm::vec2& position, const glm::vec2& size, GLuint texture, bool closeButton = true);
    uint32_t createText(uint32_t parent, const std::string& text, const glm::vec2& position, const glm::vec2& scale, const glm::vec4& color, UIElement::Position alignment = UIElement::Position::Center, UIElement::Position anchor = UIElement::Position::Center);
    uint32_t createButton(uint32_t overlay, const glm::vec2& size, const glm::vec4& color, float spacing, int index, GenericCallback onClick);

    void updateWeapons(float dt);
    void updatePlayer(GLFWwindow* window, const glm::vec2& cursorScenePosition, float dt);
    void updateEnemyAI(float dt);
    void updateHealth(float dt);
    void updateUI();
    void updateHoveredUIElement(const glm::vec2& cursorUIPosition);
    // void updateDepotOverlay();
    void updateDeliveryOverlay();
    // void updateStoreOverlay();
    void updateStoreOverlayItems();
    // void updateBehaviors(float dt);
    void updateTemporaries(float dt);
    void updateZombieLevel(float dt);
    void updatePauseOverlay();

    void setCharacterFlipHorizontal(uint32_t index, bool flipHorizontal);
    void onWeaponCollision(uint32_t index, uint32_t other, const CollisionRecord& collisionRecord);
    void onTriggerCollision(uint32_t index, uint32_t other, const CollisionRecord& collisionRecord);
    void onTriggerDepotOverlay();
    void onPlayerDied(uint32_t index);
    bool hasDeliveryForAddress(uint32_t address);
    void completeDelivery();
    void closeButtonClicked(uint32_t index);
    void overlayDeliveryItemClicked(uint32_t index);
    void storeOverlayItemClicked(uint32_t index);
    void showStoreOverlay();
    void showDeliveryOverlay();
    void closeDepotOverlay();
    void showGameOverOverlay();
};
