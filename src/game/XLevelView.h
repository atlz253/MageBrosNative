// Stage 3 level viewer: a level's geometry and objects, drawn with the original's textures.
//
// Geometry comes from MapGeometry (XMap::_ConstructFloorAndWall). Objects follow the dispatch in
// XMap::_ConstructObjects: the goal pad, coins and torches use their real models; every other
// object (platforms, static objects, fences, enemies, bosses) is shown as a colour-coded marker
// until its class is reconstructed in stage 4.
//
// Part of the reconstructed game code (src/game, LICENSE part 2).
#pragma once
#include "core/d3dtypes.h"
#include "core/level.h"
#include "game/XMapCollision.h"
#include "game/XMapGeometry.h"
#include <map>
#include <memory>
#include <functional>
#include <string>
#include <vector>

class Device;
class CD3DFile;
class MeshBuffer;

class XLevelView {
public:
    XLevelView();
    ~XLevelView();

    // world 0..3, level 0..9. Returns false if the level can't be read.
    bool Load(int world, int level);
    // `drawObjects`: also draw the coins and torches (the level viewer); the game draws its own.
    void Render(Device* d, bool drawObjects = true);

    int World() const { return world_; }
    int Level() const { return level_; }
    int Depth() const { return grid_.depth; }
    // XCamera::ResetForGame: the far limit of the camera's look-at point, (rows - 4) * 40.
    float CameraMaxZ() const;
    // XGameObject::InitLevel: Music06 if the level has a boss, else Music02..05 for worlds 1..4.
    int MusicTrack() const;
    // XMap::GetFloorLevel at a world position (ConvertPosXToMapX / Z with the floor flag set).
    int FloorAt(float x, float z) const;
    // A row's flag: the first character of its line ('C' candle, 'W' water, '-' nothing)
    char RowFlag(int z) const { return (z < 0 || z >= grid_.depth) ? '-' : grid_.Flag(z); }
    // XMap::UseWater: a row's flag is 'W' (XMap::_ConstructWallObjects)
    bool UseWater() const {
        for (char f : grid_.rowFlags)
            if (f == 'W') return true;
        return false;
    }
    // XMap's collision with this level.
    const MapCollision& Collision() const { return collision_; }
    // A short description for the window title.
    std::string Summary() const;

    // Object categories, as XMap::_ConstructObjects sorts them.
    enum class Kind { Goal, Coin, Torch, TorchSmall, Fence, Platform, StaticObject, Enemy, Boss, Other };
    struct Object {
        Kind kind;
        char code;          // the character in the level's object field
        int x, z, floor;    // cell and its floor level (after bridges are resolved)
        D3DVECTOR pos;      // centre of the cell at floor height
    };
    const std::vector<Object>& Objects() const { return objects_; }
    // Keep markers only for the objects `keep` accepts (the game hides the ones it builds).
    void FilterMarkers(const std::function<bool(const Object&)>& keep);
    // The object field's character at a cell ('.' outside the map).
    char ObjectCode(int x, int z) const {
        return (x < 0 || x >= LevelGrid::kWidth || z < 0 || z >= grid_.depth) ? '.' : grid_.Object(z, x);
    }

private:
    void PlaceObjects();
    void BuildMarkers(const std::function<bool(const Object&)>& keep = nullptr);
    void DrawMesh(Device* d, MapGeometry::Mesh& m, std::unique_ptr<MeshBuffer>& buffer, uint16_t texture);

    int world_ = -1, level_ = -1;
    LevelGrid grid_;
    MapGeometry::Result geo_;
    MapCollision collision_;
    uint16_t floorTexture_ = 0, undergroundTexture_ = 0;
    std::vector<Object> objects_;
    MapGeometry::Mesh goalPads_;  // the goal's 88x88 pad (texture 420)
    MapGeometry::Mesh markers_;   // coloured boxes, untextured
    std::unique_ptr<MeshBuffer> floorBuffer_, undergroundBuffer_, goalBuffer_, markerBuffer_;
};
