#pragma once
#include "core/Math.h"
#include "world/Mesher.h"
#include "gfx/GL.h"
#include "gfx/Shader.h"
#include "gfx/LightClusters.h"
#include <algorithm>
#include <array>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace qc {
struct Camera {
    vec3 pos;
    float yaw = 0, pitch = 0, roll = 0, fov = 70.0f * DEG2RAD;
    float aspect = 16.0f / 9.0f, nearZ = 0.05f, farZ = 400.0f;
    mat4 view, proj, viewProj;
    Frustum frustum;
    vec3 forward() const { return {std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw)}; }
    void update();
};

struct SkyState {
    float sunLight = 1.0f;
    vec3 skyTop{0.35f, 0.55f, 0.95f}, skyHorizon{0.70f, 0.82f, 1.0f}, fogColor{0.70f, 0.82f, 1.0f};
    vec3 sunDir{0, 1, 0};
    float skyExposure = 1;
    float starAlpha = 0;
    float fogStart = 60, fogEnd = 120;
    bool underwater = false, inLava = false;
    float cloudHeight = 124.0f;
    bool clouds = true;
    vec3 lightDir{0, 1, 0};
    vec3 sunCol{1, 1, 1};
    vec3 ambCol{0.3f, 0.35f, 0.45f};
    float fogDensity = 0.005f;
    bool castShadows = true;
    float overcast = 0;
};

struct BoxInstance {
    vec4 r0, r1, r2;
    vec4 layers;
    vec4 params;
    vec4 tint{1, 1, 1, 1};
};

struct LineVertex {
    vec3 p;
    uint32_t color;
};

class Renderer {

public:
    bool init(int texLayers, const std::vector<uint32_t>& texels);
    void shutdown();
    void resize(int w, int h, float renderScale, int forceW = 0, int forceH = 0);
    int width() const { return w_; }
    int height() const { return h_; }

    void uploadSection(int cx, int cz, int sy, const MeshOut& m);
    void freeSection(int cx, int cz, int sy);
    uint32_t sectionVersion(int64_t key) const {
        auto it = sectionVer_.find(key);
        return it == sectionVer_.end() ? 0 : it->second;
    }
    void freeColumn(int cx, int cz);
    bool hasSection(int cx, int cz, int sy) const;
    int uploadMesh(const MeshOut& m);
    void freeMesh(int id);
    void setBodyLightMeshes(const std::vector<int>& ids);

    void setPointLights(std::vector<PointLight> lights);
    void beginFrame(const Camera& cam, const SkyState& sky, float time);
    void drawSky();
    void drawTerrain(bool translucent);
    void drawMesh(int id, const mat4& modelCamRel, vec3 uvOrigin, float skyScale);
    bool bodyVisible(vec3 centerCamRelative, float radius, bool shadowPass) const;
    void drawBoxes(const std::vector<BoxInstance>& boxes, bool blend, const mat4* viewProjOverride = nullptr, bool depth = true,
                   int unlit = -1);
    void drawLines(const std::vector<LineVertex>& lines, bool depth = true);
    void drawClouds();
    void endWorld();
    void drawFullscreenTint(vec4 color);

    void setFancyWater(bool on);
    void setShadows(int size, float radius, bool volumetric);
    void setCaveCulling(bool on) { caveCull_ = on; }
    void setDetailDistance(float nearM, float farM) {
        detailNear_ = nearM;
        detailFar_ = farM;
    }
    void setAspect43(bool on) { aspect43_ = on; }
    void setPointShadowLimit(int n) { pointShadowLimit_ = std::max(0, n); }
    bool shadowsActive() const { return shadowSize_ > 0 && sky_.castShadows; }
    void beginShadow();
    void drawMeshShadow(int id, const mat4& modelCamRel, vec3 uvOrigin);
    void drawBoxesShadow(const std::vector<BoxInstance>& boxes);
    void endShadow();
    int pointShadowCount() const {
        return shadowSize_ > 0 ? std::min({pointCacheCapacity_, pointShadowLimit_, int(pointLights_.size())}) : 0;
    }
    const PointLight& pointLight(int index) const { return pointLights_[index]; }
    bool pointShadowNeedsUpdate(int light, const std::vector<BoxInstance>& boxes, uint64_t bodyStamp);
    void beginPointShadow(int light, int face);
    void endPointShadows();
    void initLightVolume(int size);
    bool liveLightColumn(int cx, int cz) const {
        return lightVol_ && cx * CS >= lvMinX_ + CS && cz * CS >= lvMinZ_ + CS && (cx + 1) * CS <= lvMinX_ + lvN_ - CS &&
               (cz + 1) * CS <= lvMinZ_ + lvN_ - CS;
    }
    void uploadLightBlock(int tx, int ty, int tz, const uint8_t* rg, const uint8_t* rgb);
    void invalidateLightColumn(int slotX, int slotZ);
    void setLightWindow(int minX, int minZ) {
        lvMinX_ = minX;
        lvMinZ_ = minZ;
    }
    struct Stats {
        int sections = 0, drawn = 0, quads = 0, meshes = 0, drawCalls = 0, shadowRenders = 0, pointTerrainPasses = 0, pointLivePasses = 0,
            clusteredLights = 0, clusterMax = 0, clusterReferences = 0, shadowCacheHits = 0, shadowDeferred = 0;
        size_t gpuBytes = 0;
    } stats;
    struct GpuTimes {
        float shadow = 0, terrain = 0, entities = 0, water = 0, post = 0, ui = 0, total = 0;
        bool valid = false;
    } gpu;
    void gpuFrameEnd();
    bool multiDraw() const { return mdi_; }
    GLuint textureArray() const { return texArray_; }
    float time() const { return time_; }
    const Camera& camera() const { return cam_; }
    const SkyState& sky() const { return sky_; }

private:
    struct GpuMesh {
        GLuint vao = 0, vbo = 0, bodyLight = 0;
        std::array<int, 3> bodyLightSize{}, bodyLightOrigin{};
        int solidQuads = 0, transQuads = 0;
        size_t bytes = 0;
    };
    struct GpuSec {
        uint32_t off = 0, cap = 0;
        uint32_t opaque = 0, cutout = 0, trans = 0, detail = 0;
        std::array<uint32_t, 7> opaqueFaces{}, cutoutFaces{};
        uint64_t shadowHash = 0;
        vec3 center;
        float appearAt = -1000.0f;
    };
    void ensureIndices(int quads);
    void createMesh(GpuMesh& g, const MeshOut& m);
    void destroyMesh(GpuMesh& g);
    uint32_t arenaAlloc(uint32_t quads);
    void arenaFree(uint32_t off, uint32_t quads);
    void arenaGrow(uint32_t need);
    void releaseSection(GpuSec& g);
    enum Part { P_OPAQUE, P_CUTOUT, P_TRANS, P_DETAIL };
    std::vector<const GpuSec*> detailList_;
    float detailNear_ = 40.0f, detailFar_ = 56.0f;
    void drawSections(const std::vector<const GpuSec*>& list, Part part, bool reverse, bool directionCull = false);
    void renderStaticShadow();
    void gpuMark(int i);
    int shadowNear(vec3 center) const;
    void terrainChanged(vec3 center);
    void setTerrainUniforms(Shader& s);
    void setLightingUniforms(Shader& s);
    void uploadClusterLights();
    void initPointAtlas();
    void setVolumeUniforms(Shader& s, bool use);

    int w_ = 1, h_ = 1, rw_ = 1, rh_ = 1;
    float scale_ = 1;
    GLuint fbo_ = 0, fboColor_ = 0, fboDepth_ = 0;
    GLuint depthFbo_ = 0, depthTex_ = 0;
    static constexpr int MaxPointCaches = 48;
    GLuint pointShadowFbo_ = 0, pointShadowAtlas_ = 0, pointStaticFbo_ = 0, pointStaticAtlas_ = 0;
    struct PointCache {
        uint64_t id = 0, movingStamp = 0, lastUsed = 0;
        vec3 pos;
        float radius = 0, time = 0;
        bool valid = false, refresh = true, liveValid = false;
        int resolution = 128;
    };
    std::array<PointCache, MaxPointCaches> pointCache_{};
    int pointCacheCapacity_ = MaxPointCaches;
    int pointShadowLimit_ = 12;
    uint64_t lightFrame_ = 0;
    int pointUpdates_ = 0, pointTerrainUpdates_ = 0;
    std::vector<int> pointSlots_;
    LightClusters clusters_;
    GLuint clusterRanges_ = 0, clusterIndices_ = 0, lightData_ = 0;
    int clusterIndexRows_ = 0;
    Frustum pointFaceFrustum_;
    std::vector<BoxInstance> pointBoxes_;
    std::vector<BoxInstance> boxCull_;
    int pointShadowValid_ = 0, pointShadowIndex_ = 0;
    bool pointShadowPass_ = false;
    static constexpr int pointShadowSize_ = 256;
    GLuint shadowFbo_ = 0, shadowTex_ = 0;
    GLuint shadowStaticFbo_ = 0, shadowStaticTex_ = 0;
    bool shadowLive_ = false;
    void ensureLiveShadow();
    GLuint sunShadowTexture() const { return shadowLive_ ? shadowTex_ : shadowStaticTex_; }
    int shadowSize_ = 0, shadowAlloc_ = 0;
    float shadowRadius_ = 64;
    bool volumetric_ = false;
    bool aspect43_ = false;
    mat4 shadowVP_;
    mat4 shadowStaticVP_;
    vec3 shadowCam_, shadowLight_;
    bool shadowValid_ = false, shadowSoftDirty_ = false;
    float shadowTime_ = 0;
    uint32_t terrainEpoch_ = 1, shadowEpoch_ = 0;
    GLuint volFbo_ = 0, volTex_ = 0;
    int vw_ = 1, vh_ = 1;
    Shader shadow_, shadowOpaque_, post_, vol_;
    GLuint arenaVbo_ = 0, arenaVao_ = 0, originBuf_ = 0, indirectBuf_ = 0;
    uint32_t arenaCap_ = 0;
    std::map<uint32_t, uint32_t> free_;
    bool mdi_ = false;
    struct DrawCmd {
        uint32_t count, instances, firstIndex;
        int32_t baseVertex;
        uint32_t baseInstance;
    };
    std::vector<DrawCmd> cmds_;
    std::vector<float> origins_;
    size_t originCap_ = 0, cmdCap_ = 0;
    static constexpr int kMarks = 7, kRing = 4;
    GLuint queries_[kRing][kMarks] = {};
    int qFrame_ = 0, marked_ = 0;
    bool qIssued_[kRing] = {};
    bool fancyWater_ = true;
    GLuint lightVol_ = 0, colorVol_ = 0, lightValidity_ = 0;
    int lvN_ = 0, lvMinX_ = 0, lvMinZ_ = 0;
    GLuint texArray_ = 0, ebo_ = 0, cloudTex_ = 0;
    int eboQuads_ = 0;
    GLuint boxVao_ = 0, boxVbo_ = 0, boxInst_ = 0, lineVao_ = 0, lineVbo_ = 0, emptyVao_ = 0, cloudVao_ = 0, cloudVbo_ = 0;
    size_t boxInstCap_ = 0, lineCap_ = 0;
    Shader terrain_, terrainOpaque_, terrainOpaqueFade_, terrainWater_, waterDepth_, box_, line_, sky_s_, cloud_, tint_;
    std::unordered_map<int64_t, GpuSec> sections_;
    std::unordered_map<int64_t, uint64_t> secVis_;
    std::unordered_set<int64_t> reached_;
    bool caveCull_ = true;
    void floodVisible();
    std::unordered_map<int64_t, uint32_t> sectionVer_;
    std::unordered_map<int, GpuMesh> meshes_;
    std::array<int, 4> bodyLightMeshes_{};
    int nextMesh_ = 1;
    std::vector<PointLight> pointLights_;

    int cloudCount_ = 0, cloudCellX_ = 0x7fffffff, cloudCellZ_ = 0x7fffffff;
    Camera cam_;
    SkyState sky_;
    float time_ = 0;
    std::array<std::vector<const GpuSec*>, 64> visibleBuckets_;
    std::vector<const GpuSec*> visible_, shadowList_, settled_, fading_;
};

inline int64_t sectionKey(int cx, int cz, int sy) {
    return (int64_t(uint32_t(cx) & 0xFFFFFFu) << 32) | (int64_t(uint32_t(cz) & 0xFFFFFFu) << 8) | int64_t(sy);
}

}
