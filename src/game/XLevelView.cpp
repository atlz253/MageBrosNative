#include "XLevelView.h"
#include "core/assets.h"
#include "core/game_files.h"
#include "core/log.h"
#include "framework/d3dfile.h"
#include "framework/d3dtextr.h"
#include "framework/d3dutil.h"
#include "game/JModel.h"
#include "gfx/device.h"
#include "gfx/mesh_buffer.h"
#include <cstdio>

namespace {

// XMap::XMap and GetUndergroundTexture: the floor and underground textures of each world.
const uint16_t kFloorTexture[GameFiles::kWorldCount] = {0x9c, 0xfb, 0xfc, 0x2b5};
const uint16_t kUndergroundTexture[GameFiles::kWorldCount] = {0x15b, 0x291, 0x15b, 0x291};
const uint16_t kGoalTexture = 0x1a4;     // XMap::_CreateGoal
const uint16_t kCoinModel = 0x180;       // XCoin: Object_Coin
const uint16_t kTorchModel = 0x186;      // XTorch: Object_Torch
const uint16_t kTorchSmallModel = 0x18b; // Object_TorchSmall

constexpr float kCoinScale = 0.35f;
constexpr float kHalfPi = 1.5707964f;

// Marker colours for the objects not reconstructed yet.
D3DCOLOR MarkerColor(XLevelView::Kind k) {
    switch (k) {
        case XLevelView::Kind::Platform: return 0xFF3A7BD5;      // blue
        case XLevelView::Kind::StaticObject: return 0xFF9C6B3C;  // brown
        case XLevelView::Kind::Fence: return 0xFFA0A0A0;         // grey
        case XLevelView::Kind::Enemy: return 0xFFE04040;         // red
        case XLevelView::Kind::Boss: return 0xFFD040D0;          // magenta
        default: return 0xFFFFFFFF;
    }
}

XLevelView::Kind Classify(char c) {
    using K = XLevelView::Kind;
    if (c == '#') return K::Goal;
    if (c == '$') return K::Coin;
    if (c == '*') return K::Torch;
    if (c == '+') return K::TorchSmall;
    // XMap::_ConstructObjects builds a fence only for '/'; the fence doors 'v' 'w' are enemies
    // (they count as fence only when a fence piece picks its shape from its neighbours)
    if (c == '/') return K::Fence;
    if (c >= 'A' && c <= 'G') return K::Platform;
    if (c >= 'H' && c <= 'Z') return K::StaticObject;
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '5')) return K::Enemy;
    if (c >= '6' && c <= '9') return K::Boss;
    return K::Other;  // '-', '!' and empty cells
}

void AddQuad(MapGeometry::Mesh& m, const D3DVECTOR c[4], D3DCOLOR color, float u0 = 0, float v0 = 0, float u1 = 1,
             float v1 = 1) {
    WORD base = (WORD)m.vertices.size();
    const float uv[4][2] = {{u0, v0}, {u1, v0}, {u0, v1}, {u1, v1}};
    for (int i = 0; i < 4; i++) {
        D3DLVERTEX v{};
        v.x = c[i].x;
        v.y = c[i].y;
        v.z = c[i].z;
        v.color = color;
        v.tu = uv[i][0];
        v.tv = uv[i][1];
        m.vertices.push_back(v);
    }
    static const WORD kIdx[6] = {0, 1, 3, 0, 3, 2};
    for (WORD i : kIdx) m.indices.push_back((WORD)(base + i));
}

// A box of half-size h around `p`, faces shaded so it reads as a solid without lighting.
void AddBox(MapGeometry::Mesh& m, const D3DVECTOR& p, float h, D3DCOLOR color) {
    auto shade = [&](float f) {
        DWORD r = (DWORD)(((color >> 16) & 0xFF) * f), g = (DWORD)(((color >> 8) & 0xFF) * f), b = (DWORD)((color & 0xFF) * f);
        return 0xFF000000u | r << 16 | g << 8 | b;
    };
    float x0 = p.x - h, x1 = p.x + h, y0 = p.y - h, y1 = p.y + h, z0 = p.z - h, z1 = p.z + h;
    // both windings are drawn (culling is off for markers), so face order doesn't matter
    D3DVECTOR top[4] = {{x0, y1, z1}, {x1, y1, z1}, {x0, y1, z0}, {x1, y1, z0}};
    D3DVECTOR front[4] = {{x1, y1, z0}, {x1, y1, z1}, {x1, y0, z0}, {x1, y0, z1}};
    D3DVECTOR back[4] = {{x0, y1, z1}, {x0, y1, z0}, {x0, y0, z1}, {x0, y0, z0}};
    D3DVECTOR left[4] = {{x0, y1, z0}, {x1, y1, z0}, {x0, y0, z0}, {x1, y0, z0}};
    D3DVECTOR right[4] = {{x1, y1, z1}, {x0, y1, z1}, {x1, y0, z1}, {x0, y0, z1}};
    AddQuad(m, top, shade(1.0f));
    AddQuad(m, front, shade(0.8f));
    AddQuad(m, back, shade(0.5f));
    AddQuad(m, left, shade(0.65f));
    AddQuad(m, right, shade(0.65f));
}

}  // namespace

XLevelView::XLevelView() = default;
XLevelView::~XLevelView() = default;

bool XLevelView::Load(int world, int level) {
    uint16_t id = GameFiles::LevelResourceId(world, level);
    const auto* d = Assets::RCData(id);
    std::string err;
    LevelGrid g;
    if (!d || !ParseLevel(d->data(), d->size(), &g, &err)) {
        Log("Level view: world %d level %d (resource %u) unreadable: %s", world + 1, level + 1, id, err.c_str());
        return false;
    }
    world_ = world;
    level_ = level;
    grid_ = std::move(g);
    geo_ = MapGeometry::Build(grid_);
    floorBuffer_.reset();
    undergroundBuffer_.reset();
    collision_ = MapCollision(geo_.cells, grid_.depth);
    floorTexture_ = kFloorTexture[world];
    undergroundTexture_ = kUndergroundTexture[world];
    for (uint16_t t : {floorTexture_, undergroundTexture_, kGoalTexture})
        if (!D3DTextr_GetSurface(t)) D3DTextr_CreateTextureFromResource(t, 0, 0);
    PlaceObjects();
    BuildMarkers();
    Log("Level view: %s", Summary().c_str());
    return true;
}

void XLevelView::PlaceObjects() {
    objects_.clear();
    for (int z = 0; z < grid_.depth; z++)
        for (int x = 0; x < LevelGrid::kWidth; x++) {
            char c = grid_.Object(z, x);
            Kind k = Classify(c);
            if (k == Kind::Other) continue;
            int floor = MapGeometry::FloorLevel(geo_.cells, grid_.depth, x, z);
            float y = (floor < 0 ? 0 : floor) * MapGeometry::kLevelHeight;
            objects_.push_back({k, c, x, z, floor,
                                D3DVECTOR(x * MapGeometry::kCellSize + 20.0f, y, z * MapGeometry::kCellSize + 20.0f)});
        }
}

void XLevelView::FilterMarkers(const std::function<bool(const Object&)>& keep) { BuildMarkers(keep); }

void XLevelView::BuildMarkers(const std::function<bool(const Object&)>& keep) {
    goalPads_ = MapGeometry::Mesh();
    markers_ = MapGeometry::Mesh();
    goalBuffer_.reset();
    markerBuffer_.reset();
    for (const auto& o : objects_) {
        if (keep && o.kind != Kind::Goal && !keep(o)) continue;
        if (o.kind == Kind::Goal) {
            // XMap::_CreateGoal: an 88x88 pad just above the floor
            float h = 44.0f, y = o.pos.y + 0.1f;
            D3DVECTOR c[4] = {{o.pos.x - h, y, o.pos.z + h}, {o.pos.x + h, y, o.pos.z + h},
                              {o.pos.x - h, y, o.pos.z - h}, {o.pos.x + h, y, o.pos.z - h}};
            AddQuad(goalPads_, c, 0xFFFFFFFF);
        } else if (o.kind != Kind::Coin && o.kind != Kind::Torch && o.kind != Kind::TorchSmall) {
            float size = o.kind == Kind::Boss ? 16.0f : 8.0f;
            AddBox(markers_, D3DVECTOR(o.pos.x, o.pos.y + size, o.pos.z), size, MarkerColor(o.kind));
        }
    }
}

int XLevelView::FloorAt(float x, float z) const {
    if (x < 0 || z < 0) return -1;
    return MapGeometry::FloorLevel(geo_.cells, grid_.depth, (int)(x / MapGeometry::kCellSize),
                                   (int)(z / MapGeometry::kCellSize));
}

void XLevelView::DrawMesh(Device* d, MapGeometry::Mesh& m, std::unique_ptr<MeshBuffer>& buffer, uint16_t texture) {
    if (m.vertices.empty()) return;
    if (!buffer) buffer = std::make_unique<MeshBuffer>();
    d->SetTexture(0, texture ? D3DTextr_GetSurface(texture) : nullptr);
    d->DrawMesh(*buffer, D3DFVF_LVERTEX, m.vertices.data(), (DWORD)m.vertices.size(), m.indices.data(),
                (DWORD)m.indices.size(), 0, (DWORD)m.indices.size());
}

void XLevelView::Render(Device* d, bool drawObjects) {
    if (world_ < 0) return;
    D3DMATRIX world;
    D3DUtil_SetIdentityMatrix(world);
    d->SetTransform(D3DTRANSFORMSTATE_WORLD, &world);

    // pre-lit geometry (D3DLVERTEX), as the original draws its merged map meshes
    d->SetRenderState(D3DRENDERSTATE_LIGHTING, 0);
    d->SetRenderState(D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
    DrawMesh(d, geo_.floorMesh, floorBuffer_, floorTexture_);
    DrawMesh(d, geo_.undergroundMesh, undergroundBuffer_, undergroundTexture_);
    DrawMesh(d, goalPads_, goalBuffer_, kGoalTexture);
    DrawMesh(d, markers_, markerBuffer_, 0);
    d->SetTexture(0, nullptr);

    // objects with real models
    d->SetRenderState(D3DRENDERSTATE_LIGHTING, 1);
    d->SetRenderState(D3DRENDERSTATE_CULLMODE, D3DCULL_CCW);
    for (const auto& o : objects_) {
        if (!drawObjects) break;
        uint16_t id = 0;
        float scale = 1.0f, yaw = 0.0f;
        if (o.kind == Kind::Coin) { id = kCoinModel; scale = kCoinScale; }
        else if (o.kind == Kind::Torch) { id = kTorchModel; yaw = kHalfPi; }
        else if (o.kind == Kind::TorchSmall) { id = kTorchSmallModel; yaw = kHalfPi; }
        else continue;
        // models from the shared list carry the global -90 degree X rotation and x20 scale
        JObject3DX obj(id);
        obj.SetScale(scale);
        obj.SetYaw(yaw);
        obj.SetPos(o.pos);
        obj.UpdateWorldMatrix();
        obj.Render(d);
    }
    D3DUtil_SetIdentityMatrix(world);
    d->SetTransform(D3DTRANSFORMSTATE_WORLD, &world);
}

float XLevelView::CameraMaxZ() const {
    float z = (grid_.depth - 4) * MapGeometry::kCellSize;
    return z < 160.0f ? 160.0f : z;
}

int XLevelView::MusicTrack() const {
    // XGameObject::InitLevel: Music06 when the level has a boss (XGameObject::boss is set by the
    // boss codes '6'..'9' in the object field: every world's level 10, and World 4's levels 2, 5
    // and 8), else Music02..05 for worlds 1..4.
    for (const auto& o : objects_)
        if (o.kind == Kind::Boss) return 6;
    return world_ + 2;
}

std::string XLevelView::Summary() const {
    int counts[10] = {};
    for (const auto& o : objects_) counts[(int)o.kind]++;
    char b[256];
    std::snprintf(b, sizeof b,
                  "World %d-%d: %d rows, %zu + %zu quads, %d coins, %d torches, %d platforms, %d objects, %d fences, "
                  "%d enemies, %d bosses",
                  world_ + 1, level_ + 1, grid_.depth, geo_.floorMesh.QuadCount(), geo_.undergroundMesh.QuadCount(),
                  counts[(int)Kind::Coin], counts[(int)Kind::Torch] + counts[(int)Kind::TorchSmall],
                  counts[(int)Kind::Platform], counts[(int)Kind::StaticObject], counts[(int)Kind::Fence],
                  counts[(int)Kind::Enemy], counts[(int)Kind::Boss]);
    return b;
}
