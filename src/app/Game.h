#pragma once
#include "platform/Audio.h"
#include "core/Tasks.h"
#include "gfx/Renderer.h"
#include "gfx/UI.h"
#include "platform/Input.h"
#include "world/Gen.h"
#include "player/Player.h"
#include "player/HeldItem.h"
#include "sim/Sim.h"
#include "app/Sounds.h"
#include "stream/LightVolume.h"
#include "stream/Streamer.h"
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;

namespace qc {
struct Settings {
    int renderDist = 16;
    float fov = 70;
    float sensitivity = 1.0f;
    int guiScale = 0;
    int graphics = 0;
    int atmosphere = 0;
    bool turnSway = true;
    bool sprintPulse = true;
    bool stepShake = true;
    bool handDrift = true;
    bool aspect43 = true;
    bool fancyLeaves = true, fancyWater = true, clouds = true, bobbing = true, vsync = true, fullscreen = false;
    float renderScale = 1.0f;
    float volume = 0.8f;
    int physics = 1;
    void load(const std::string& path);
    void save(const std::string& path) const;
    void potato();
};

enum class Screen { Title, Worlds, Create, CreateWorldGen, Confirm, Loading, Playing, Pause, Inventory, Options, Controls };

class Game {

public:
    bool init(GLFWwindow* win, Audio* audio, Input* input);
    void frame(float dt);
    void shutdown();
    bool quit = false;

    std::vector<std::string> script;
    std::string screenshotPath;
    bool headless = false;

private:
    bool createWorld(const std::string& name, const std::string& seedText, const GenSettings& genSettings);
    bool openWorld(const std::string& folder);
    void closeWorld(bool save);
    void saveLevel();
    void refreshWorldList();
    void applySettings();

    struct Skiff {
        vec3 pos, vel;
        float yaw = 0, stroke = 0, yawVel = 0;
    };
    struct RopeSpan {
        vec3 a, b;
        ivec3 ca{}, cb{};
    };
    std::vector<Skiff> skiffs_;
    struct LanternCache {
        uint64_t version = 0;
        bool valid = false;
        std::vector<int> lanterns;
    };
    mutable std::unordered_map<int64_t, LanternCache> lanternCache_;
    std::vector<RopeSpan> ropes_;
    int ridingSkiff_ = -1, ridingRope_ = -1;
    float ropeT_ = 0, ropeSpeed_ = 0;
    bool hooked_ = false, ropePending_ = false, lanternOn_ = true;
    vec3 hookPoint_, ropeStart_;
    ivec3 hookCell_{}, ropeStartCell_{};
    float hookLength_ = 0;
    bool useAdventure();
    bool toggleRide(bool retrieve = false);
    void adventureStep(float dt, const PlayerInput& in);
    void drawAdventure(std::vector<BoxInstance>& out) const;
    void gatherPointLights();
    struct EmitterCache {
        uint64_t stamp = ~uint64_t(0);
        std::vector<PointLight> lights;
    };
    std::unordered_map<int64_t, EmitterCache> emitterCache_;
    void loadAdventure();
    void saveAdventure() const;
    void updatePlaying(float dt, bool controls);
    void interact(float dt);
    std::vector<ivec3> breakCells(const RayHit& h, bool mini) const;
    std::vector<ivec3> placeCells(const RayHit& h, bool mini, ItemId item) const;
    void dropHeld(bool all);
    void doBreak();
    bool doPlace();
    void runScript();

    void renderWorld();
    void computeSky(SkyState& s) const;
    void updateClimate(float dt);
    void drawAmbient(std::vector<BoxInstance>& blended);
    float rng01() { return fxRng_.uniform(); }
    Rng fxRng_{0x5EA7u};
    float climateT_ = 0, temp_ = 0.5f, humid_ = 0.5f;
    void drawHeld();
    void drawSelection();

    void drawHUD();
    void drawDebug();
    void drawBackground(bool dark);
    void drawTitle();
    void drawWorlds();
    void drawCreate();
    void drawCreateWorldGen();
    void drawConfirm();
    void drawLoading();
    void drawPause();
    void drawInventory();
    void drawOptions();
    void drawControls();
    bool slotClick(ItemStack& slot, bool right, bool shift, int slotIndex);
    void closeInventory();
    int segRow(float x, float& yy, float labW, float segW, const char* label, int value, std::initializer_list<const char*> opts);
    float card(float w, float h);
    int optionsTab_ = 0;
    int itemCategory(ItemId id) const;
    int quickPrev_ = 0;
    vec2 fakeMouse_;
    int fakeMouseFrames_ = 0;
    bool fakeHover_ = false;
    double lastSlotClick_ = -10;
    int lastSlotIdx_ = -1, creativeCat_ = 0, searchFocus_ = -1;
    std::string creativeSearch_;
    void setScreen(Screen s);
    bool escPressed() const { return ui_.in.escape && clock_ - screenTime_ > 0.08; }
    double screenTime_ = 0;

    GLFWwindow* win_ = nullptr;
    Audio* audio_ = nullptr;
    Input* input_ = nullptr;
    TaskPool pool_;
    World world_;
    std::unique_ptr<Generator> gen_;
    Streamer streamer_;
    Sim sim_;
    Renderer renderer_;
    LightVolume lightVol_;
    UI ui_;
    Sounds sounds_;
    Player player_;
    Settings settings_;
    Screen screen_ = Screen::Title, optionsReturn_ = Screen::Title;
    bool worldLoaded_ = false, firstSpawn_ = false;
    std::string worldDir_, worldName_;
    uint64_t seed_ = 0;
    float dayTime_ = 0.02f;
    double clock_ = 0;
    float autosave_ = 0;
    float fps_ = 60, fpsAccum_ = 0;
    int fpsFrames_ = 0;
    int fbW_ = 1, fbH_ = 1;

    bool miniMode_ = false, showDebug_ = false, hideHud_ = false;
    float frameDt_ = 0.016f;

    bool viewMotionInit_ = false;
    float viewLastYaw_ = 0, viewLastPitch_ = 0, viewYawRate_ = 0, viewPitchRate_ = 0;
    float viewSideSway_ = 0, viewVertSway_ = 0, viewRollSway_ = 0;
    float viewModelYawLag_ = 0, viewModelPitchLag_ = 0, viewModelRollLag_ = 0;
    float zoomAmount_ = 0;
    vec3 scriptMove_;
    float scriptMoveT_ = 0, scriptYawRate_ = 0;
    RayHit target_;
    int targetEnt_ = -1;
    std::vector<ivec3> targetCells_, previewCells_;
    int64_t mineKey_ = 0;
    struct FeedLine {
        ItemId item;
        int count;
        float age;
    };
    std::vector<FeedLine> feed_;
    float slotPop_[9] = {};
    int lastCount_[9] = {};
    ItemId lastId_[9] = {};
    float useCooldown_ = 0, attackCooldown_ = 0, swing_ = 0, breakCooldown_ = 0;
    float heldNameTimer_ = 0;
    int lastSelected_ = -1;
    float lastWTap_ = -10;
    bool sprintToggle_ = false;

    ItemStack cursor_;
    int creativeScroll_ = 0;

    struct WorldInfo {
        std::string folder, name;
        double lastPlayed = 0;
    };
    std::vector<WorldInfo> worlds_;
    int selectedWorld_ = -1;
    std::string newName_ = "New World", newSeed_;
    GenSettings newGen_{};
    GenSettings genSettings_{};
    int focus_ = -1;
    std::string message_;
    float loadingTime_ = 0;

    std::vector<BoxInstance> boxes_, blended_;
    Camera cam_;
    int scriptWait_ = 0;
    double msUpdate_ = 0, msRender_ = 0, msLightVol_ = 0;
    double lastWall_ = 0, wallMs_ = 0;
    float streamUpdateAccum_ = 0, lightVolUpdateAccum_ = 0;
    float rustleT_ = 0;
    double msStream_ = 0, msDrain_ = 0;
};

}
