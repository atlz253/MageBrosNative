#include "XLevelLoop.h"
#include "framework/d3dtextr.h"
#include "framework/d3dutil.h"
#include "framework/interp.h"
#include "game/XLevelView.h"
#include "game/XMath.h"
#include "game/XBoss.h"
#include "game/XBullet.h"
#include "game/XCandle.h"
#include "game/XEnemy.h"
#include "game/XGameContext.h"
#include "game/XParticle.h"
#include "game/XPlatform.h"
#include "game/XStaticObj.h"
#include "game/XWater.h"
#include "gfx/device.h"
#include <cmath>
#include <cstdlib>

namespace {
const uint16_t kSoundGoal = 0xea;       // the open goal's hum
const uint16_t kSoundCoin = 0x268;
const uint16_t kSoundTorchOn = 0x26c;
const uint16_t kTextureFireA = 0x15c;   // player 1's shot
const uint16_t kTextureFireB = 0x184;   // player 2's
const uint16_t kModelTorchLarge = 0x186;
const uint16_t kModelTorchSmall = 0x18b;
const uint16_t kModelCoin = 0x180, kModelHeart = 0x2ac;
const uint16_t kSoundCoinBounce = 0x27b;
constexpr int kMaxFires = 30;
constexpr float kFireSpeed = 2.6f;
}  // namespace

XLevelLoop::XLevelLoop()
    : game_(std::make_unique<XGameContext>()), particles_(std::make_unique<XParticleList>()) {
    statics_ = std::make_unique<XStaticObjList>(game_.get());
    enemies_ = std::make_unique<XEnemyList>(game_.get());
    bullets_ = std::make_unique<XBulletList>(game_.get());
    platforms_ = std::make_unique<XPlatformList>(game_.get());
    water_ = std::make_unique<XWater>();
    candles_ = std::make_unique<XCandleList>(game_.get());
    // XHeroFire's look: an 18-unit particle (0.8 of that for the small shots of some wands)
    fireSpriteA_ = std::make_unique<JObject3DRect>(JObject3DRect::Particle(18.0f, 18.0f));
    fireSpriteA_->SetTexture(kTextureFireA);
    fireSpriteB_ = std::make_unique<JObject3DRect>(JObject3DRect::Particle(18.0f, 18.0f));
    fireSpriteB_->SetTexture(kTextureFireB);
    fireSpriteSmallA_ = std::make_unique<JObject3DRect>(JObject3DRect::Particle(18.0f, 18.0f));
    fireSpriteSmallA_->Scale(0.8f);
    fireSpriteSmallA_->SetTexture(kTextureFireA);
    fireSpriteSmallB_ = std::make_unique<JObject3DRect>(JObject3DRect::Particle(18.0f, 18.0f));
    fireSpriteSmallB_->Scale(0.8f);
    fireSpriteSmallB_->SetTexture(kTextureFireB);
    particles_->SetHeroPos([this](bool heroA) {
        const XHero* h = heroA ? heroA_ : heroB_;
        return h ? h->GetPos() : D3DVECTOR(0, 0, 0);
    });
    particles_->SetPlaySound([this](uint16_t id, float z) {
        if (sound_) sound_(id, z);
    });
}
XLevelLoop::~XLevelLoop() = default;

void XLevelLoop::Load(const XLevelView& level, XHero* heroA, XHero* heroB, bool onePlayer, SoundFn sound) {
    level_ = &level;
    heroA_ = heroA;
    heroB_ = heroB;
    onePlayer_ = onePlayer;
    sound_ = std::move(sound);
    fires_.assign(kMaxFires, Fire());
    torches_.clear();
    coins_.clear();
    litTorches_ = 0;
    hasBoss_ = false;
    mapCounter_ = 0;
    readyForWarp_ = false;
    // XGameObject::InitLevel empties every list (and deletes the last level's boss)
    game_->boss = nullptr;
    boss_.reset();
    enemies_->Empty();
    bullets_->Empty();
    platforms_->Empty();
    candles_->Empty();
    statics_->Empty();
    particles_->Empty();
    // XWater::Deinit, then XWater::Init on a water level
    water_->Deinit();
    useWater_ = level.UseWater();
    if (useWater_) water_->Init(LevelGrid::kWidth, level.Depth(), level.World());

    XGameContext& g = *game_;
    g.level = &level;
    g.heroA = heroA;
    g.heroB = heroB;
    g.heroWorld = this;
    g.statics = statics_.get();
    g.enemies = enemies_.get();
    g.bullets = bullets_.get();
    g.platforms = platforms_.get();
    g.candles = candles_.get();
    g.particles = particles_.get();
    g.playSound = [this](uint16_t id, float z) { sound_(id, z); };
    g.playMono = [this](uint16_t id, float volume) {
        if (playMono_) playMono_(id, volume);
    };
    g.stopMusic = [this] {
        if (stopMusic_) stopMusic_();
    };
    g.startShaking = [this](float magnitude) {
        if (shake_) shake_(magnitude);
    };
    g.startShop = [this](XHero* hero) {
        if (shop_) shop_(hero);
    };
    g.createEnemyCoin = [this](const XEnemyBase& e) { CreateEnemyCoin(e, (int)e.GetSizeType()); };
    g.createHeart = [this](const XEnemyBase& e) { CreateEnemyCoin(e, 2); };
    g.barrelCounter = &barrelCounter_;  // (kept for the whole game, as XGameObject's)
    g.onePlayer = onePlayer;
    g.world = level.World();
    g.useWater = useWater_;

    // XMap::_ConstructWallObjects: a candle on the back wall of each row flagged 'C'
    for (int z = 0; z < level.Depth(); z++)
        if (level.RowFlag(z) == 'C') candles_->CreateCandle(D3DVECTOR(0.0f, 0.0f, z * 40.0f + 20.0f));

    // XMap::_ConstructObjects: each cell in turn, at the centre of the cell at its floor's height
    auto isFence = [&](int x, int z) {
        char c = level.ObjectCode(x, z);
        return c == '/' || c == 'v' || c == 'w';
    };
    for (const auto& o : level.Objects()) {
        D3DVECTOR at(o.pos.x, o.floor * 25.0f, o.pos.z);
        switch (o.kind) {
            case XLevelView::Kind::Goal:  // XMap::_CreateGoal
                goalPos_ = D3DVECTOR(o.pos.x, (o.floor < 0 ? 0 : o.floor) * 25.0f + 0.1f, o.pos.z);
                break;
            case XLevelView::Kind::Torch:
            case XLevelView::Kind::TorchSmall: {  // XTorch::XTorch
                Torch t;
                t.large = o.kind == XLevelView::Kind::Torch;
                t.pos = at;
                float h = t.large ? 76.0f : 50.0f;
                t.model = std::make_unique<JObject3DX>(t.large ? kModelTorchLarge : kModelTorchSmall);
                t.model->SetYaw(1.5707964f);
                t.model->SetPos(at);
                t.model->UpdateWorldMatrix();
                statics_->CreateCollideBox(at, 28.0f, h, 28.0f);  // the post, as tall as the torch
                t.fireCheckPos = D3DVECTOR(at.x, at.y + h, at.z);
                t.fireSpawnPos = D3DVECTOR(at.x + 8.0f, at.y + h + 14.5f, at.z);
                torches_.push_back(std::move(t));
                break;
            }
            case XLevelView::Kind::Coin: {  // '$': a medium coin (XCoin::SetFloorLevel)
                Coin& c = CreateCoin(at, 1);
                c.floorLevel = o.floor;
                c.shadow->SetFloorLevel(o.floor);
                break;
            }
            case XLevelView::Kind::Fence: {
                // the shape from the fence cells (fences and fence doors) on each side
                bool l = isFence(o.x - 1, o.z), r = isFence(o.x + 1, o.z);
                bool zm = isFence(o.x, o.z - 1), zp = isFence(o.x, o.z + 1);
                char shape = 'X';
                if (l && !r && !zp && zm) shape = '1';
                else if (l && !r && zp && !zm) shape = '2';
                else if (!l && r && !zp && zm) shape = '3';
                else if (!l && r && zp && !zm) shape = '4';
                else if (l && r && !zp && zm) shape = '5';
                else if (l && !r && zp && zm) shape = '6';
                else if (!l && r && zp && zm) shape = '7';
                else if (l && r && zp && !zm) shape = '8';
                else if ((l || r) && !zp && !zm) shape = 'X';
                else if (!l && !r && (zp || zm)) shape = 'Z';
                // (a lone fence or a four-way crossing leaves the original's shape undefined)
                if (XStaticObjBase* f = statics_->CreateFence(at, shape)) f->SetFloorLevel(o.floor);
                break;
            }
            case XLevelView::Kind::StaticObject:
                if (XStaticObjBase* so = statics_->CreateObject(at, o.code)) so->SetFloorLevel(o.floor);
                break;
            case XLevelView::Kind::Enemy:
                if (XEnemyBase* e = enemies_->CreateEnemy(at, o.code)) e->SetFloorLevel(o.floor);
                break;
            case XLevelView::Kind::Boss:  // XMap::_ConstructObjects: '6' Dragon, '7' Genie, '8' Nest, '9' Wizard
                hasBoss_ = true;
                if (o.code == '6') boss_ = std::make_unique<XBoss_Dragon>(game_.get(), at);
                else if (o.code == '7') boss_ = std::make_unique<XBoss_Genie>(game_.get(), at);
                else if (o.code == '8') boss_ = std::make_unique<XBoss_Nest>(game_.get(), at);
                else if (o.code == '9') boss_ = std::make_unique<XBoss_Wizard>(game_.get(), at);
                g.boss = boss_.get();
                break;
            case XLevelView::Kind::Platform:  // XPlatformList::CreatePlatform
                if (XPlatformBase* p = platforms_->CreatePlatform(at, o.code)) p->SetFloorLevel(o.floor);
                break;
            default:
                break;
        }
    }
}

int XLevelLoop::BossHealth() const { return boss_ ? boss_->GetHealthMain() : 0; }

bool XLevelLoop::IsReconstructed(char code) {
    static const char kDone[] = "#$*+/OPQSa15opb3d0jmvwHIJKABCDEFGi6TUh2xyc4kqrse7MNfzuRtg89";
    for (const char* c = kDone; *c; c++)
        if (*c == code) return true;
    return false;
}

// ---------------------------------------------------------------------------- the goal

void XLevelLoop::CloseWarpZone() {
    mapCounter_ = 0;
    readyForWarp_ = false;
}

// ---------------------------------------------------------------------------- shots

// XHeroFireList::CreateFire: the pattern of the shooter's wand (XHeroStatus::wandType). The
// double wands fire two shots 6 degrees either side of the hero's yaw (the homing one 8); the
// triple wands one straight ahead and two small ones 12 degrees either side (the homing one 16).
void XLevelLoop::CreateFire(const XHero& hero) {
    const float yaw = hero.GetYaw();
    switch (hero.Status().wandType) {
    case 0:  // _WandNormal
        CreateFire_Normal(hero, yaw, true);
        break;
    case 1:  // _WandNormal2
        CreateFire_Normal(hero, NormaliseAngle(yaw + 0.10472f), true);
        CreateFire_Normal(hero, NormaliseAngle(yaw - 0.10472f), true);
        break;
    case 2:  // _WandNormal3
        CreateFire_Normal(hero, yaw, true);
        CreateFire_Normal(hero, NormaliseAngle(yaw + 0.20944f), false);
        CreateFire_Normal(hero, NormaliseAngle(yaw - 0.20944f), false);
        break;
    case 3:  // _WandChase
        CreateFire_Chase(hero, yaw, true);
        break;
    case 4:  // _WandChase2
        CreateFire_Chase(hero, NormaliseAngle(yaw + 0.139626f), true);
        CreateFire_Chase(hero, NormaliseAngle(yaw - 0.139626f), true);
        break;
    case 5:  // _WandChase3
        CreateFire_Chase(hero, yaw, true);
        CreateFire_Chase(hero, NormaliseAngle(yaw + 0.279253f), false);
        CreateFire_Chase(hero, NormaliseAngle(yaw - 0.279253f), false);
        break;
    }
}

// Create8Way's shots (the hero bursting as it dies) are always plain and large
void XLevelLoop::CreateFireAt(const XHero& hero, float angle) { CreateFire_Normal(hero, angle, true); }

// XHeroFire::XHeroFire: 20 units out along `angle` and 12 up; radius 8, or 6.4 for a small shot
XLevelLoop::Fire* XLevelLoop::NewFire(const XHero& hero, float angle, bool large) {
    for (Fire& f : fires_) {
        if (f.live) continue;
        const D3DVECTOR& p = hero.GetPos();
        f = Fire();
        f.live = true;
        f.owner = &hero;
        f.angle = angle;
        f.pos = D3DVECTOR(p.x + std::sin(angle) * 20.0f, p.y + 12.0f, p.z + std::cos(angle) * 20.0f);
        f.large = large;
        if (!large) f.radius *= 0.8f;
        return &f;
    }
    return nullptr;
}

void XLevelLoop::CreateFire_Normal(const XHero& hero, float angle, bool large) { NewFire(hero, angle, large); }

// XHeroFire_Chase::XHeroFire_Chase: the target is the chasable enemy nearest a point 100 ahead of
// the shot, among those within 200 of that point along Z; with none, the boss if it is in its
// NORMAL state and within 200 of the shot along Z
void XLevelLoop::CreateFire_Chase(const XHero& hero, float angle, bool large) {
    Fire* f = NewFire(hero, angle, large);
    if (!f) return;
    f->chase = true;
    D3DVECTOR ahead(f->pos.x + std::sin(f->angle) * 100.0f, f->pos.y, f->pos.z + std::cos(f->angle) * 100.0f);
    f->targetEnemy = enemies_->GetClosestEnemy(ahead, 200.0f);
    if (!f->targetEnemy && boss_ && boss_->GetLiveState() == XBossBase::NORMAL &&
        std::fabs(boss_->GetPos().z - f->pos.z) < 200.0f)
        f->targetBoss = boss_.get();
}

void XLevelLoop::CreateEnemyExplode(const D3DVECTOR& pos, int size) { particles_->CreateEnemyExplode(pos, size != 0); }
void XLevelLoop::CreateLandEffect(const D3DVECTOR& pos) { particles_->CreateLandEffect(pos); }
void XLevelLoop::CreateHeroWarpPhoton(bool heroA) { particles_->CreateHeroWarpPhoton(heroA); }
void XLevelLoop::CreateWaterWave(const D3DVECTOR& pos, bool large, bool sound) {
    particles_->CreateWaterWave(pos, large, sound);
}

bool XLevelLoop::TorchCollisionCheckFire(const Fire& f) {
    for (Torch& t : torches_) {
        if (t.fireOn) continue;
        if (std::fabs(t.pos.z - f.pos.z) > 40.0f) continue;
        if (ApproxDistance(f.pos, t.fireCheckPos) < t.fireCheckRadius + f.radius) {
            // XTorch::_TurnOnFire
            sound_(kSoundTorchOn, t.pos.z);
            particles_->CreateTorchFire1(t.fireSpawnPos);
            t.fireOn = true;
            litTorches_++;
            return true;
        }
    }
    return false;
}

void XLevelLoop::ProcessFire(Fire& f) {
    f.prevPos = f.pos;  // native (smooth motion): drawn from here
    f.prevTick = Interp::CurrentTick();
    // XHeroFire_Chase::ProcessFrame: after 10 ticks, turn at most 1.6 degrees a tick towards the
    // target (wherever it is: the target is kept even once it has been defeated)
    if (f.chase && f.localTime > 10) {
        if (f.targetEnemy)
            f.angle = MakeAngleCloseTo_Constant(f.angle, FindDirectionXZ(f.pos, f.targetEnemy->GetPos()), 0.0279253f);
        else if (f.targetBoss)
            f.angle = MakeAngleCloseTo_Constant(f.angle, FindDirectionXZ(f.pos, f.targetBoss->GetPos()), 0.0279253f);
    }
    f.localTime++;
    const D3DVECTOR prev = f.pos;  // the flash goes where the shot was before this move
    f.pos.x += std::sin(f.angle) * kFireSpeed;
    f.pos.z += std::cos(f.angle) * kFireSpeed;
    if (f.localTime > 20) {
        f.verticalSpeed -= 0.06f;
        f.pos.y += f.verticalSpeed;
    }
    bool ownerA = f.owner->IsHeroA();
    if (f.localTime % 2 == 0) particles_->CreateHeroFireSmoke(f.pos, ownerA, f.large);
    // XHeroFire::ProcessFrame: the map, either hero, objects, platforms, enemies, bullets,
    // candles, the boss stop the shot with a flash
    if (level_->Collision().IsBlockedFloat(f.pos.x, f.pos.z, f.pos.y) || (heroA_ && heroA_->CollisionCheck(f.pos)) ||
        (heroB_ && heroB_->CollisionCheck(f.pos)) || statics_->CollisionCheck_Fire(f.pos) ||
        platforms_->CollisionCheck(f.pos) || enemies_->CollisionCheck_Fire(f.pos, f.radius, f.owner->GetPos()) ||
        bullets_->CollisionCheck_Fire(f.pos, f.radius) || candles_->CollisionCheck(f.pos) || (boss_ && boss_->CollisionCheck_Fire(f.pos, f.radius))) {
        particles_->CreateHeroFireFlash(prev, ownerA, f.large);
        f.live = false;
    } else if (TorchCollisionCheckFire(f)) {
        f.live = false;
    } else if (f.pos.y < -200.0f) {
        f.live = false;
    } else if (useWater_ && f.pos.y < -38.0f) {
        // into the water: a small wave, unless it was already deeper than -43
        if (f.pos.y > -43.0f) particles_->CreateWaterWave(D3DVECTOR(f.pos.x, -38.0f, f.pos.z), false, true);
        f.live = false;
    }
}

// ---------------------------------------------------------------------------- per tick

void XLevelLoop::SetVisibleRange(float range) { game_->visibleRange = range; }
void XLevelLoop::SetUpdateRange(float range) { game_->updateRange = range; }

void XLevelLoop::ProcessBeforeHero(float cameraZ) {
    game_->cameraZ = cameraZ;
    // XMap::ProcessFrame
    mapCounter_++;
    if (!readyForWarp_) {
        // every torch lit, or on a boss level the boss defeated (gone for 500 ticks)
        if (mapCounter_ > 300) {
            if (!hasBoss_) {
                if (litTorches_ >= (int)torches_.size()) readyForWarp_ = true;
            } else if (boss_ && boss_->BossDefeated()) {
                readyForWarp_ = true;
            }
        }
    }
    if (readyForWarp_) {
        if (mapCounter_ % 60 == 0) particles_->CreateWarpRing(D3DVECTOR(goalPos_.x, goalPos_.y + 0.5f, goalPos_.z));
        if (mapCounter_ % 120 == 0) sound_(kSoundGoal, goalPos_.z);
    }
    water_->ProcessFrame();  // XWater::ProcessFrame
    particles_->ProcessFrame();
    for (Fire& f : fires_)
        if (f.live) ProcessFire(f);
    bullets_->ProcessFrame();  // XBulletList::ProcessFrame
}

bool XLevelLoop::CoinTouchesHero(const Coin& c, const XHero* hero) {
    if (!hero || hero->GetLiveState() != XHero::NORMAL) return false;
    const D3DVECTOR& hp = hero->GetPos();
    const XCollideBox& box = hero->GetCollideBox();
    if (std::fabs(c.pos.z - hp.z) >= box.WidthZ() + c.radius) return false;
    if (std::fabs(c.pos.x - hp.x) >= box.WidthX() + c.radius) return false;
    // JObject3DX::CheckCollision_Sphere: the coin's sphere against the hero's mesh (MageCOL)
    return hero->BodyModel().CheckCollision_Sphere(D3DVECTOR(c.pos.x, c.pos.y + c.radius, c.pos.z), c.radius);
}

XLevelLoop::Coin& XLevelLoop::CreateCoin(const D3DVECTOR& pos, int type) {
    // XCoin::XCoin: small coins (model 0x180 at 0.35) are worth 3 (5 in a two-player game), medium
    // ones (0.6) 30 (50); hearts (model 0x2ac at 0.6) give a life
    Coin c;
    c.type = type;
    c.pos = pos;
    float scale = type == 0 ? 0.35f : 0.6f;
    c.model = std::make_unique<JObject3DX>(type == 2 ? kModelHeart : kModelCoin);
    c.model->SetScale(scale);
    c.gold = type == 0 ? (onePlayer_ ? 3 : 5) : type == 1 ? (onePlayer_ ? 30 : 50) : 0;
    c.shadow = std::make_unique<XShadowSimple>(scale * 40.0f);
    c.radius = scale * 40.0f / 2.0f;
    c.rotSpeed = 1.5f;
    c.yaw = UnitRand() * 6.28319f;
    c.model->SetYaw(c.yaw);
    c.model->SetPos(c.pos);
    c.model->UpdateWorldMatrix();
    coins_.push_back(std::move(c));
    return coins_.back();
}

void XLevelLoop::CreateEnemyCoin(const XEnemyBase& e, int type) {
    // XCoinList::CreateEnemyCoin (by the enemy's size: small, medium, heart) and CreateHeart: a
    // coin spinning fast and bouncing out
    Coin& c = CreateCoin(e.GetPos(), type);
    c.floorLevel = e.GetFloorLevel();
    c.shadow->SetFloorLevel(c.floorLevel);
    c.rotSpeed = 15.0f;
    c.bouncing = true;
}

void XLevelLoop::ProcessCoin(Coin& c) {
    // XCoin::ProcessFrame
    c.localCounter++;
    if (!c.live) return;
    if (c.bouncing) {  // _ProcessFrame_Bounce
        if (c.localCounter == 1) c.verticalSpeed = 3.0f;
        c.pos.y += c.verticalSpeed;
        c.verticalSpeed -= 0.08f;
        if (c.verticalSpeed < -4.0f) c.verticalSpeed = -4.0f;
        if (c.floorLevel >= 0 && c.floorLevel * 25.0f > c.pos.y) {
            c.verticalSpeed = std::fabs(c.verticalSpeed) * 0.8f;
            if (c.verticalSpeed > 1.6f) sound_(kSoundCoinBounce, c.pos.z);
            if (c.verticalSpeed < 0.8f) {
                c.pos.y = c.floorLevel * 25.0f;
                c.bouncing = false;
            }
        }
        if (c.pos.y < -200.0f) c.live = false;
    }
    if (c.rotSpeed > 1.8f) c.rotSpeed -= 0.02f;
    c.yaw = NormaliseAngle(c.yaw + 0.0174533f * c.rotSpeed);
    c.model->SetYaw(c.yaw);
    c.model->SetPos(c.pos);
    c.model->UpdateWorldMatrix();
    // hero A first: a coin both touch goes to player 1
    CoinCollisionCheckHero(c, heroA_);
    CoinCollisionCheckHero(c, heroB_);
}

void XLevelLoop::CoinCollisionCheckHero(Coin& c, XHero* hero) {
    if (!c.live || !CoinTouchesHero(c, hero)) return;
    sound_(kSoundCoin, c.pos.z);
    particles_->CreateGetCoinEffect(D3DVECTOR(c.pos.x, c.pos.y + c.radius, c.pos.z));
    // XHeroBase::IncreaseLife / IncreaseGold: that hero's status
    XHeroStatus& st = hero->Status();
    if (c.type == 2) {
        if (++st.life > 5) st.life = 5;
    } else {
        int g = st.gold + (int)c.gold;
        st.gold = (int16_t)(g > 9999 ? 9999 : g);
    }
    c.live = false;
}

void XLevelLoop::TorchCollisionCheckHero(const Torch& t, XHero* hero) {
    // the flame burns a hero standing in it
    if (!hero || hero->GetLiveState() != XHero::NORMAL) return;
    if (std::fabs(t.pos.z - hero->GetPos().z) > 80.0f) return;
    if (ApproxDistance(hero->GetPos(), t.fireCheckPos) < t.heroCheckRadius) {
        hero->Damage(*this);
        hero->KnockBack(t.pos);
    }
}

void XLevelLoop::ProcessAfterHero() {
    // XGameObject::ProcessFrame after the heroes: enemies, the boss, coins, torches, platforms,
    // objects
    enemies_->ProcessFrame();
    if (boss_) boss_->ProcessFrame();
    for (size_t i = 0; i < coins_.size(); i++) ProcessCoin(coins_[i]);
    // XTorchList::ProcessFrame
    for (Torch& t : torches_) {
        t.localCounter++;
        if (!t.fireOn) continue;
        if (t.localCounter % 12 == 0) particles_->CreateTorchFire2(RandomPosXZ(t.fireCheckPos, 2.5f));
        TorchCollisionCheckHero(t, heroA_);
        TorchCollisionCheckHero(t, heroB_);
    }
    platforms_->ProcessFrame();
    statics_->ProcessFrame();
    candles_->ProcessFrame();  // XCandleList::ProcessFrame
}

// ---------------------------------------------------------------------------- static objects
// XStaticObjList::CollisionCheckX / Z / Y for the hero's box (heights counted)

bool XLevelLoop::StaticCollisionX(float& newX, const XCollideBox& box) { return statics_->CollisionCheckX(newX, box, false); }
bool XLevelLoop::StaticCollisionZ(float& newZ, const XCollideBox& box) { return statics_->CollisionCheckZ(newZ, box, false); }
bool XLevelLoop::StaticCollisionY(float& newY, const XCollideBox& box) { return statics_->CollisionCheckY(newY, box); }

// ---------------------------------------------------------------------------------- platforms
// XPlatformList::CollisionCheckX / Z / Y for the hero's box (heights counted)

bool XLevelLoop::PlatformCollisionX(float& newX, const XCollideBox& box) { return platforms_->CollisionCheckX(newX, box, false); }
bool XLevelLoop::PlatformCollisionZ(float& newZ, const XCollideBox& box) { return platforms_->CollisionCheckZ(newZ, box, false); }
bool XLevelLoop::PlatformCollisionY(float& newY, const XCollideBox& box) { return platforms_->CollisionCheckY(newY, box); }
bool XLevelLoop::CandleCollisionX(float& newX, const XCollideBox& box) { return candles_->CollisionCheckX(newX, box, false); }
bool XLevelLoop::CandleCollisionZ(float& newZ, const XCollideBox& box) { return candles_->CollisionCheckZ(newZ, box, false); }
bool XLevelLoop::CandleCollisionY(float& newY, const XCollideBox& box) { return candles_->CollisionCheckY(newY, box); }

void XLevelLoop::SetHeroAboveLastPlatform(bool heroA) {
    if (XPlatformBase* p = platforms_->GetLastCollidePlatform()) {
        if (heroA) p->SetHeroAAbove(true);
        else p->SetHeroBAbove(true);
    }
}

// ---------------------------------------------------------------------------- drawing

void XLevelLoop::RenderObjects(Device* d, const D3DMATRIX& view) {
    // XGameObject::Render, after the map and the heroes: the coins, then the torches
    d->SetRenderState(D3DRENDERSTATE_LIGHTING, 1);
    for (Coin& c : coins_) {  // XCoin::Render
        if (!c.live || !game_->IsVisible(c.pos)) continue;
        c.model->Render(d);
        c.shadow->SetPosXZ(c.pos + c.model->BlendOffset());  // (native: where the model is drawn)
        c.shadow->Render(d);
    }
    for (Torch& t : torches_)
        if (game_->IsVisible(t.pos)) t.model->Render(d);
    // platforms, static objects, candles, bullets, enemies, the boss
    platforms_->Render(d);
    statics_->Render(d);
    candles_->Render(d);
    bullets_->Render(d, view);
    enemies_->Render(d);
    if (boss_) boss_->Render(d);
    water_->Render(d);  // XWater::Render, before the particles
}

void XLevelLoop::RenderEffects(Device* d, const D3DMATRIX& view) {
    // last: the particles, then the shots (XHeroFireList::Render)
    particles_->Render(d, view);
    for (const Fire& f : fires_) {
        if (!f.live) continue;
        JObject3DRect& sprite = f.owner->IsHeroA() ? (f.large ? *fireSpriteA_ : *fireSpriteSmallA_)
                                                   : (f.large ? *fireSpriteB_ : *fireSpriteSmallB_);
        sprite.SetWorldPos(Interp::BlendPos(f.prevPos, f.pos, f.prevTick));  // (native: smooth motion)
        sprite.Render2(d, view);
    }
}
