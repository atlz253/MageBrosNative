#include "game/XEnemy.h"
#include "core/config.h"
#include "framework/d3dutil.h"
#include "game/JModel.h"
#include "game/XGameContext.h"
#include "game/XHero.h"
#include "game/XLevelView.h"
#include "game/XMath.h"
#include "game/XParticle.h"
#include "game/XPlatform.h"
#include "game/XStaticObj.h"
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace {
constexpr uint16_t kSoundEnemyHurt = 0x270, kSoundEnemyDie = 0x27d, kSoundEnemyBurst = 0x26b;
constexpr float kPi = 3.14159f, kHalfPi = 1.5708f, kQuarterPi = 0.785398f;

// RandomI: lo..hi, including hi when `inclusive`
long RandomI(long lo, long hi, bool inclusive) { return Random(hi - lo + (inclusive ? 1 : 0)) + lo; }

// ------------------------------------------------------------------------------- XEnemy_Slime
// 'a' slime (1 health), '1' king slime (20), '5' queen slime (120, starts at x 100). It walks a
// short way, waits, and every few waits sits for 60 ticks then turns towards a random point (or
// jumps at a hero within 120).
class Slime : public XEnemyBase {
public:
    enum MoveState { NON = 0, WALK = 1, WAIT = 2, JUMP = 3, LONGWAIT = 4 };
    Slime(const XGameContext* g, int oid, const D3DVECTOR& p, SizeType size) : XEnemyBase(g, oid, p, size) {
        struct Look {
            uint16_t frames[4], col;
            float shadow, width, height;
            int health;
        };
        static const Look kLooks[3] = {
            {{0x16f, 0x16e, 0x16d, 0x16e}, 0x177, 34.0f, 34.0f, 18.0f, 1},
            {{0x1e1, 0x1e2, 0x1e3, 0x1e2}, 0x1e4, 68.0f, 68.0f, 36.0f, 20},
            {{0x2c2, 0x2c0, 0x2c1, 0x2c0}, 0x2bf, 163.2f, 163.2f, 86.4f, 120},
        };
        if (size == QUEEN) {
            startPos_.x = 100.0f;
            pos_ = startPos_;
        }
        const Look& l = kLooks[size];
        model_ = std::make_unique<JObject3DX>(l.frames[0]);
        for (int i = 1; i < 4; i++) model_->AppendFrame(l.frames[i]);
        model_->SetCollisionModel(l.col);
        shadow_ = std::make_unique<XShadowSimple>(l.shadow);
        collideBox_ = std::make_unique<XCollideBox>(l.width, l.height);
        collideBox_->SetPos(startPos_);
        health_ = l.health;
        dir_ = UnitRand() * 6.28319f;
        yaw_ = dir_;
        UpdateWorldMatrix();
        countdownLongWait_ = (int)Random(5) + 2;
        countdownJump_ = (int)Random(4) + 3;
        chasable_ = true;
        liveState_ = XEnemyBase::NORMAL;
        moveState_ = WALK;
    }

    void ProcessFrame() override {
        if (!game_->IsUpdatable(pos_)) return;
        XEnemyBase::ProcessFrame();
        if (liveState_ != XEnemyBase::NORMAL) return;
        switch (moveState_) {
            case WALK: Walk(); break;
            case WAIT: Wait(); break;
            case JUMP: Jump(); break;
            case LONGWAIT: LongWait(); break;
            default: break;
        }
        yaw_ = MakeAngleCloseTo(yaw_, dir_, sizeType_ == QUEEN ? 0.02f : 0.06f, 0.0f, FLT_MAX);
        UpdateWorldMatrix();
        CollisionCheck_Hero(game_->heroA);
        CollisionCheck_Hero(game_->heroB);
    }

private:
    void Walk() {
        float speed = 0.5f;
        if (sizeType_ == KING) speed *= 1.5f;
        else if (sizeType_ == QUEEN) speed = 0.0f;
        if (localCounter_ == 1) prevPosForWalk_ = pos_;
        float nx = pos_.x + std::sin(dir_) * speed, nz = pos_.z + std::cos(dir_) * speed;
        UpdatePosX(nx);
        UpdatePosZ(nz);
        frame_ = (int)((localCounter_ / 10 + 1) % 4);
        if (localCounter_ >= 40) {
            frame_ = 0;
            // stuck against something: no long wait this time
            if (sizeType_ != QUEEN && ApproxDistance(prevPosForWalk_, pos_) < 10.0f) countdownLongWait_ = 0;
            localCounter_ = 0;
            moveState_ = WAIT;
        }
    }
    void Wait() {
        if (localCounter_ < 10) return;
        if (--countdownLongWait_ <= 0) {
            countdownLongWait_ = (int)Random(5) + 2;
            localCounter_ = 0;
            moveState_ = LONGWAIT;
        } else {
            localCounter_ = 0;
            moveState_ = WALK;
        }
    }
    void Jump() {
        unsigned duration = 100;
        float height = 60.0f, speedUp = 0.2f, speedDown = 1.1f;
        if (sizeType_ == KING) {
            height *= 1.5f, speedUp *= 1.5f, speedDown *= 1.5f;
        } else if (sizeType_ == QUEEN) {
            duration = 120;
            height *= 2.5f;
            speedUp = speedDown = 0.0f;
        }
        float nx = pos_.x, nz = pos_.z;
        if (localCounter_ < duration / 2) {  // crouching up: slowly, tilting back
            nx += std::sin(dir_) * speedUp;
            nz += std::cos(dir_) * speedUp;
            frame_ = 2;
            pitch_ = MakeAngleCloseTo(pitch_, -0.349066f, 0.06f, 0.0f, FLT_MAX);
        } else {  // coming down: fast, tilting forward
            nx += std::sin(dir_) * speedDown;
            nz += std::cos(dir_) * speedDown;
            frame_ = 1;
            pitch_ = MakeAngleCloseTo(pitch_, 0.349066f, 0.06f, 0.0f, FLT_MAX);
        }
        float ny = floorLevel_ * 25.0f + std::sin((float)localCounter_ / (float)duration * kPi) * height;
        UpdatePosX(nx);
        UpdatePosZ(nz);
        UpdatePosY(ny);
        if (localCounter_ >= duration) {
            frame_ = 0;
            pitch_ = 0;
            localCounter_ = 0;
            moveState_ = WAIT;
        }
    }
    void LongWait() {
        if (localCounter_ < 60) return;
        countdownJump_--;
        dir_ = MakeAngleCloseTo_Constant(dir_, FindDirectionXZ(pos_, GenerateRandomPos()), 2.0944f);
        if (countdownJump_ <= 0) {
            countdownJump_ = (int)Random(4) + 3;
            if (game_->DistanceToCloserHero(pos_) < 120.0f) dir_ = game_->DirToCloserHero(pos_);
            localCounter_ = 0;
            moveState_ = JUMP;
        } else {
            localCounter_ = 0;
            moveState_ = WALK;
        }
    }

    MoveState moveState_ = NON;
    int countdownLongWait_ = 0, countdownJump_ = 0;
    D3DVECTOR prevPosForWalk_{0, 0, 0};
};

// ------------------------------------------------------------------------------ XEnemy_Needle
// Floor spikes: 34 wide, 70 tall, sunk 40.5 into the floor. 'p' spikes sink 30 and rise again
// over 50 ticks each, waiting (200 - 20 x world) ticks in between; 'o' spikes stay. They can't be
// hurt, and don't hurt a hero who is still flashing.
class Needle : public XEnemyBase {
public:
    enum MoveState { NON = 0, WAIT = 1, WAIT2 = 2, MOVEUP = 3, MOVEDOWN = 4 };
    Needle(const XGameContext* g, int oid, const D3DVECTOR& p, bool movable)
        : XEnemyBase(g, oid, p, SMALL), movable_(movable) {
        model_ = std::make_unique<JObject3DX>(0x18a);
        model_->SetCollisionModel(0x1c8);
        collideBox_ = std::make_unique<XCollideBox>(34.0f, 70.0f);
        collideBox_->SetPos(startPos_);
        startPos_.y -= 40.5f;
        SetPosY(startPos_.y);
        UpdateWorldMatrix();
        noCollisionWhenHeroDamaged_ = true;
        damagable_ = false;
        liveState_ = XEnemyBase::NORMAL;
        moveState_ = WAIT;
    }

    void ProcessFrame() override {
        XEnemyBase::ProcessFrame();
        if (liveState_ != XEnemyBase::NORMAL) return;
        unsigned wait = (unsigned)(200 - game_->world * 20);
        switch (moveState_) {
            case WAIT:
                if (movable_ && localCounter_ >= wait) {
                    localCounter_ = 0;
                    moveState_ = MOVEDOWN;
                }
                break;
            case WAIT2:
                if (localCounter_ >= wait) {
                    localCounter_ = 0;
                    moveState_ = MOVEUP;
                }
                break;
            case MOVEUP:
                SetPosY(startPos_.y + std::sin((float)localCounter_ / 50.0f * kHalfPi) * 30.0f - 30.0f);
                if (localCounter_ >= 50) {
                    localCounter_ = 0;
                    moveState_ = WAIT;
                }
                break;
            case MOVEDOWN:
                SetPosY(startPos_.y - std::sin((float)localCounter_ / 50.0f * kHalfPi) * 30.0f);
                if (localCounter_ >= 50) {
                    localCounter_ = 0;
                    moveState_ = WAIT2;
                }
                break;
            default: break;
        }
        UpdateWorldMatrix();
        CollisionCheck_Hero(game_->heroA);
        CollisionCheck_Hero(game_->heroB);
    }

private:
    bool movable_;
    MoveState moveState_ = NON;
};
// ------------------------------------------------------------------------- XEnemy_Stalactite
// 'u': a stalactite (model 0x1cf, collision 0x1d0, a 40 x 120 box, shadow 40) hanging at y = 220
// at a random yaw. When the closer hero is within 60 of it (in X, in Z and ApproxDistanceXZ from
// its start) it spins 4 degrees a tick; after 80 ticks it falls (gravity 0.08, at most 4; sound
// 0x277 at tick 100), and once 45 below its floor it lands: sound 0x276, camera shake 2, landing
// dust, and it stays there, still dangerous. It can't be hurt. No IsUpdatable test.
class Stalactite : public XEnemyBase {
public:
    Stalactite(const XGameContext* g, int oid, const D3DVECTOR& p) : XEnemyBase(g, oid, p, SMALL) {
        model_ = std::make_unique<JObject3DX>(0x1cf);
        model_->SetCollisionModel(0x1d0);
        shadow_ = std::make_unique<XShadowSimple>(40.0f);
        collideBox_ = std::make_unique<XCollideBox>(40.0f, 120.0f);
        collideBox_->SetPos(startPos_);
        yaw_ = UnitRand() * 6.28319f;
        SetPosY(220.0f);
        UpdateWorldMatrix();
        damagable_ = false;
        liveState_ = XEnemyBase::NORMAL;
        moveState_ = WAIT;
    }
    void ProcessFrame() override {
        XEnemyBase::ProcessFrame();
        if (liveState_ != XEnemyBase::NORMAL) return;
        if (moveState_ == WAIT) Wait();
        else if (moveState_ == DROP) Drop();
        // (STAY: nothing)
        UpdateWorldMatrix();
        CollisionCheck_Hero(game_->heroA);
        CollisionCheck_Hero(game_->heroB);
    }

private:
    enum MoveState { NON = 0, WAIT = 1, DROP = 2, STAY = 3 };
    void Wait() {
        XHero* hero = game_->GetCloserHero(startPos_);  // any live state
        if (!hero) return;
        const D3DVECTOR& hp = hero->GetPos();
        if (std::fabs(pos_.z - hp.z) > 60.0f || std::fabs(pos_.x - hp.x) > 60.0f) return;
        if (ApproxDistanceXZ(startPos_, hp) > 60.0f) return;
        localCounter_ = 0;
        moveState_ = DROP;
    }
    void Drop() {
        if (localCounter_ == 1) verticalSpeed_ = 0;
        yaw_ = NormaliseAngle(yaw_ + 0.0698132f);
        if (localCounter_ <= 80) return;
        verticalSpeed_ -= 0.08f;
        if (verticalSpeed_ < -4.0f) verticalSpeed_ = -4.0f;
        SetPosY(pos_.y + verticalSpeed_);
        if (localCounter_ == 100) game_->playSound(0x277, pos_.z);
        if (pos_.y < startPos_.y - 45.0f) {
            game_->playSound(0x276, pos_.z);
            game_->startShaking(2.0f);
            game_->particles->CreateLandEffect(D3DVECTOR(pos_.x, startPos_.y + 0.5f, pos_.z));
            localCounter_ = 0;
            moveState_ = STAY;
        }
    }
    MoveState moveState_ = NON;
};

// -------------------------------------------------------------------------- XEnemy_JumpNeedle
// 'z': spikes (model 0x293, the needle's collision 0x1c8, a 34 x 70 box, no shadow) sunk 40 into
// the floor. Once they have waited 150 ticks, a NORMAL hero (the closer one) within 64 of them
// along Z makes them shoot up over 30 ticks (sound 0x2ae at 10); they stay up 200 ticks and sink
// back over 150. They can't be hurt, and don't hurt a hero who is still flashing. No
// IsUpdatable test.
class JumpNeedle : public XEnemyBase {
public:
    JumpNeedle(const XGameContext* g, int oid, const D3DVECTOR& p) : XEnemyBase(g, oid, p, SMALL), checkPos_(p) {
        model_ = std::make_unique<JObject3DX>(0x293);
        model_->SetCollisionModel(0x1c8);
        collideBox_ = std::make_unique<XCollideBox>(34.0f, 70.0f);
        collideBox_->SetPos(startPos_);
        SetPosY(pos_.y - 40.0f);
        UpdateWorldMatrix();
        noCollisionWhenHeroDamaged_ = true;
        damagable_ = false;
        liveState_ = XEnemyBase::NORMAL;
        moveState_ = WAIT;
    }
    void ProcessFrame() override {
        XEnemyBase::ProcessFrame();
        if (liveState_ != XEnemyBase::NORMAL) return;
        if (moveState_ == WAIT) Wait();
        else if (moveState_ == WAIT2) Wait2();
        else if (moveState_ == MOVEUP) MoveUp();
        else if (moveState_ == MOVEDOWN) MoveDown();
        UpdateWorldMatrix();
        CollisionCheck_Hero(game_->heroA);
        CollisionCheck_Hero(game_->heroB);
    }

private:
    enum MoveState { NON = 0, WAIT = 1, WAIT2 = 2, MOVEUP = 3, MOVEDOWN = 4 };
    void Next(MoveState s) {
        localCounter_ = 0;
        moveState_ = s;
    }
    void Wait() {
        if (localCounter_ <= 150) return;
        XHero* hero = game_->GetCloserHero(checkPos_);
        if (hero && std::fabs(hero->GetPos().z - checkPos_.z) < 64.0f && hero->GetLiveState() == XHero::NORMAL) Next(MOVEUP);
    }
    void MoveUp() {
        SetPosY(startPos_.y + std::sin((float)localCounter_ / 30.0f * kHalfPi) * 40.0f - 40.0f);
        if (localCounter_ == 10) game_->playSound(0x2ae, pos_.z);
        if (localCounter_ >= 30) Next(WAIT2);
    }
    void Wait2() {
        if (localCounter_ >= 200) Next(MOVEDOWN);
    }
    void MoveDown() {
        SetPosY(startPos_.y - 40.0f * ((float)localCounter_ / 150.0f));
        if (localCounter_ >= 150) Next(WAIT);
    }
    MoveState moveState_ = NON;
    D3DVECTOR checkPos_;
};

// --------------------------------------------------------------------------------- XEnemy_Bee
// 'b' bee (31 x 55, 2 health), '3' king bee (62 x 110, 51 health). Hovers 5 above its floor,
// bobbing 4 up and down over 80 ticks, and drifts at 0.5 (king 0.6) for 120 ticks at a time
// towards a random point, or round behind itself after being stuck, turning at most 1 degree a
// tick; within 120 of a hero it turns towards the hero instead.
class Bee : public XEnemyBase {
public:
    Bee(const XGameContext* g, int oid, const D3DVECTOR& p, SizeType size) : XEnemyBase(g, oid, p, size) {
        bool king = size == KING;
        static const uint16_t kSmall[4] = {0x171, 0x174, 0x173, 0x174}, kKing[4] = {0x2b8, 0x2b7, 0x2b6, 0x2b7};
        const uint16_t* f = king ? kKing : kSmall;
        model_ = std::make_unique<JObject3DX>(f[0]);
        for (int i = 1; i < 4; i++) model_->AppendFrame(f[i]);
        model_->SetCollisionModel(king ? 0x2b9 : 0x178);
        shadow_ = std::make_unique<XShadowSimple>(king ? 62.0f : 31.0f);
        collideBox_ = king ? std::make_unique<XCollideBox>(62.0f, 110.0f) : std::make_unique<XCollideBox>(31.0f, 55.0f);
        collideBox_->SetPos(startPos_);
        health_ = king ? 51 : 2;
        dir_ = UnitRand() * 6.28319f;
        yaw_ = dir_;
        startPosY_ = startPos_.y + 5.0f;
        SetPosY(startPosY_);
        UpdateWorldMatrix();
        beeCounter_ = (unsigned)Random(80);
        chasable_ = true;
        liveState_ = XEnemyBase::NORMAL;
    }
    void ProcessFrame() override {
        if (!game_->IsUpdatable(pos_)) return;
        XEnemyBase::ProcessFrame();
        if (liveState_ != XEnemyBase::NORMAL) return;
        beeCounter_++;
        frame_ = (int)((beeCounter_ >> 2) % 4);
        UpdatePosY(startPosY_ + std::sin((float)(beeCounter_ % 80) / 80.0f * 6.28319f) * 4.0f);
        Walk();
        UpdateWorldMatrix();
        CollisionCheck_Hero(game_->heroA);
        CollisionCheck_Hero(game_->heroB);
    }

private:
    void Walk() {
        float speed = 0.5f, turn = 0.0174533f;
        if (sizeType_ == KING) speed *= 1.2f;
        if (localCounter_ == 1) {
            prevPosForWalk_ = pos_;
            targetPos_ = turnAround_ ? GenerateBehindPos() : GenerateRandomPos();
        }
        if (!turnAround_ && game_->DistanceToCloserHero(pos_) < 120.0f)
            dir_ = MakeAngleCloseTo_Constant(dir_, game_->DirToCloserHero(pos_), turn);
        else
            dir_ = MakeAngleCloseTo_Constant(dir_, FindDirectionXZ(pos_, targetPos_), turn);
        yaw_ = dir_;
        UpdatePosX(pos_.x + std::sin(dir_) * speed);
        UpdatePosZ(pos_.z + std::cos(dir_) * speed);
        if (localCounter_ >= 120) {
            turnAround_ = ApproxDistance(prevPosForWalk_, pos_) < 10.0f;
            localCounter_ = 0;
        }
    }
    float startPosY_ = 0;
    unsigned beeCounter_ = 0;
    bool turnAround_ = false;
    D3DVECTOR targetPos_{0, 0, 0}, prevPosForWalk_{0, 0, 0};
};

// ----------------------------------------------------------------------------- XEnemy_Wallaby
// 'd' wallaby (35 x 44, 2 health), '0' king wallaby (70 x 88, 45 health). Waits 60-240 ticks,
// then hops 2-6 times: 70 ticks in the air (up to 44 high, 0.65 forward; king 66 and 0.975)
// plus 10 on the ground, turning up to 40 degrees (king 48) towards a random point (or behind
// itself after being stuck); if a hero was within 120 when it set off, 1.5 times as fast towards
// the hero.
class Wallaby : public XEnemyBase {
public:
    Wallaby(const XGameContext* g, int oid, const D3DVECTOR& p, SizeType size) : XEnemyBase(g, oid, p, size) {
        bool king = size == KING;
        static const uint16_t kSmall[3] = {0x1a7, 0x1a8, 0x1a9}, kKing[3] = {0x1e5, 0x1e6, 0x1e7};
        const uint16_t* f = king ? kKing : kSmall;
        model_ = std::make_unique<JObject3DX>(f[0]);
        for (int i = 1; i < 3; i++) model_->AppendFrame(f[i]);
        model_->SetCollisionModel(king ? 0x1e8 : 0x1aa);
        shadow_ = std::make_unique<XShadowSimple>(king ? 70.0f : 35.0f);
        collideBox_ = king ? std::make_unique<XCollideBox>(70.0f, 88.0f) : std::make_unique<XCollideBox>(35.0f, 44.0f);
        collideBox_->SetPos(startPos_);
        health_ = king ? 45 : 2;
        dir_ = UnitRand() * 6.28319f;
        yaw_ = dir_;
        UpdateWorldMatrix();
        chasable_ = true;
        liveState_ = XEnemyBase::NORMAL;
        moveState_ = WAIT;
    }
    void ProcessFrame() override {
        if (!game_->IsUpdatable(pos_)) return;
        XEnemyBase::ProcessFrame();
        if (liveState_ != XEnemyBase::NORMAL) return;
        if (moveState_ == HOP) Hop();
        else if (moveState_ == WAIT) Wait();
        yaw_ = MakeAngleCloseTo(yaw_, dir_, 0.1f, 0.0f, FLT_MAX);
        UpdateWorldMatrix();
        CollisionCheck_Hero(game_->heroA);
        CollisionCheck_Hero(game_->heroB);
    }

private:
    enum MoveState { NON = 0, HOP = 1, WAIT = 2 };
    void Wait() {
        if (localCounter_ == 1) waitFrames_ = (unsigned)RandomI(60, 240, true);
        if (localCounter_ < waitFrames_) return;
        targetPos_ = turnAround_ ? GenerateBehindPos() : GenerateRandomPos();
        chaseHero_ = game_->DistanceToCloserHero(pos_) < 120.0f;
        countdownHop_ = (int)RandomI(2, 6, true);
        localCounter_ = 0;
        moveState_ = HOP;
    }
    void Hop() {
        float height = 44.0f, speed = 0.65f, turn = 0.698132f;
        if (sizeType_ == KING) height *= 1.5f, speed *= 1.5f, turn *= 1.2f;
        if (localCounter_ == 1) {
            prevPosForHop_ = pos_;
            if (!turnAround_ && chaseHero_)
                dir_ = MakeAngleCloseTo_Constant(dir_, game_->DirToCloserHero(pos_), turn * 1.5f);
            else
                dir_ = MakeAngleCloseTo_Constant(dir_, FindDirectionXZ(pos_, targetPos_), turn);
        }
        if (localCounter_ <= 70) {
            if (localCounter_ < 35) {
                frame_ = 1;
                pitch_ = MakeAngleCloseTo(pitch_, 0.174533f, 0.1f, 0.0f, FLT_MAX);
            } else {
                frame_ = 2;
                pitch_ = MakeAngleCloseTo(pitch_, -0.20944f, 0.1f, 0.0f, FLT_MAX);
            }
            float nx = pos_.x + std::sin(dir_) * speed, nz = pos_.z + std::cos(dir_) * speed;
            float ny = floorLevel_ * 25.0f;
            if (localCounter_ < 70) ny += std::sin((float)localCounter_ / 70.0f * kPi) * height;
            UpdatePosX(nx);
            UpdatePosZ(nz);
            UpdatePosY(ny);
        } else {
            frame_ = 0;
            pitch_ = MakeAngleCloseTo(pitch_, 0.0f, 0.2f, 0.0f, FLT_MAX);
        }
        if (localCounter_ < 80) return;
        if (!turnAround_ && ApproxDistance(prevPosForHop_, pos_) < 10.0f) {
            turnAround_ = true;  // stuck: wait, then turn round
            localCounter_ = 0;
            moveState_ = WAIT;
        } else if (--countdownHop_ <= 0) {
            turnAround_ = false;
            localCounter_ = 0;
            moveState_ = WAIT;
        } else {
            localCounter_ = 0;
            moveState_ = HOP;
        }
    }
    MoveState moveState_ = NON;
    unsigned waitFrames_ = 0;
    int countdownHop_ = 0;
    bool chaseHero_ = false, turnAround_ = false;
    D3DVECTOR targetPos_{0, 0, 0}, prevPosForHop_{0, 0, 0};
};

// ---------------------------------------------------------------------------- XEnemy_Mushroom
// 'h' mushroom (37 x 44, 3 health), '2' king mushroom (74 x 88, 30 health). The wallaby's hop
// with its own numbers: waits 50-200 ticks, then hops 3-5 times: 80 ticks in the air (up to 48
// high, 0.65 forward; king 72 and 0.975) plus 5 on the ground, turning up to 40 degrees (king
// 50) towards a random point (or behind itself after being stuck); if a hero was within 120 when
// it set off, 1.5 times as fast towards the hero.
class Mushroom : public XEnemyBase {
public:
    Mushroom(const XGameContext* g, int oid, const D3DVECTOR& p, SizeType size) : XEnemyBase(g, oid, p, size) {
        bool king = size == KING;
        static const uint16_t kSmall[3] = {0x25d, 0x25e, 0x25f}, kKing[3] = {0x285, 0x286, 0x287};
        const uint16_t* f = king ? kKing : kSmall;
        model_ = std::make_unique<JObject3DX>(f[0]);
        for (int i = 1; i < 3; i++) model_->AppendFrame(f[i]);
        model_->SetCollisionModel(king ? 0x288 : 0x260);
        shadow_ = std::make_unique<XShadowSimple>(king ? 74.0f : 37.0f);
        collideBox_ = king ? std::make_unique<XCollideBox>(74.0f, 88.0f) : std::make_unique<XCollideBox>(37.0f, 44.0f);
        collideBox_->SetPos(startPos_);
        health_ = king ? 30 : 3;
        dir_ = UnitRand() * 6.28319f;
        yaw_ = dir_;
        UpdateWorldMatrix();
        chasable_ = true;
        liveState_ = XEnemyBase::NORMAL;
        moveState_ = WAIT;
    }
    void ProcessFrame() override {
        if (!game_->IsUpdatable(pos_)) return;
        XEnemyBase::ProcessFrame();
        if (liveState_ != XEnemyBase::NORMAL) return;
        if (moveState_ == HOP) Hop();
        else if (moveState_ == WAIT) Wait();
        yaw_ = MakeAngleCloseTo(yaw_, dir_, 0.1f, 0.0f, FLT_MAX);
        UpdateWorldMatrix();
        CollisionCheck_Hero(game_->heroA);
        CollisionCheck_Hero(game_->heroB);
    }

private:
    enum MoveState { NON = 0, HOP = 1, WAIT = 2 };
    void Wait() {
        if (localCounter_ == 1) waitFrames_ = (unsigned)RandomI(50, 200, true);
        if (localCounter_ < waitFrames_) return;
        targetPos_ = turnAround_ ? GenerateBehindPos() : GenerateRandomPos();
        chaseHero_ = game_->DistanceToCloserHero(pos_) < 120.0f;
        countdownHop_ = (int)RandomI(3, 5, true);
        localCounter_ = 0;
        moveState_ = HOP;
    }
    void Hop() {
        float height = 48.0f, speed = 0.65f, turn = 0.698132f;
        if (sizeType_ == KING) height *= 1.5f, speed *= 1.5f, turn *= 1.25f;
        if (localCounter_ == 1) {
            prevPosForHop_ = pos_;
            if (!turnAround_ && chaseHero_)
                dir_ = MakeAngleCloseTo_Constant(dir_, game_->DirToCloserHero(pos_), turn * 1.5f);
            else
                dir_ = MakeAngleCloseTo_Constant(dir_, FindDirectionXZ(pos_, targetPos_), turn);
        }
        if (localCounter_ <= 80) {
            if (localCounter_ < 40) {
                frame_ = 1;
                pitch_ = MakeAngleCloseTo(pitch_, 0.174533f, 0.1f, 0.0f, FLT_MAX);
            } else {
                frame_ = 2;
                pitch_ = MakeAngleCloseTo(pitch_, -0.20944f, 0.1f, 0.0f, FLT_MAX);
            }
            float nx = pos_.x + std::sin(dir_) * speed, nz = pos_.z + std::cos(dir_) * speed;
            float ny = floorLevel_ * 25.0f;
            if (localCounter_ < 80) ny += std::sin((float)localCounter_ / 80.0f * kPi) * height;
            UpdatePosX(nx);
            UpdatePosZ(nz);
            UpdatePosY(ny);
        } else {
            frame_ = 0;
            pitch_ = MakeAngleCloseTo(pitch_, 0.0f, 0.2f, 0.0f, FLT_MAX);
        }
        if (localCounter_ < 85) return;
        if (!turnAround_ && ApproxDistance(prevPosForHop_, pos_) < 10.0f) {
            turnAround_ = true;  // stuck: wait, then turn round
            localCounter_ = 0;
            moveState_ = WAIT;
        } else if (--countdownHop_ <= 0) {
            turnAround_ = false;
            localCounter_ = 0;
            moveState_ = WAIT;
        } else {
            localCounter_ = 0;
            moveState_ = HOP;
        }
    }
    MoveState moveState_ = NON;
    unsigned waitFrames_ = 0;
    int countdownHop_ = 0;
    bool chaseHero_ = false, turnAround_ = false;
    D3DVECTOR targetPos_{0, 0, 0}, prevPosForHop_{0, 0, 0};
};

// ------------------------------------------------------------------------------- XEnemy_Squid
// 'c' squid (40 x 60, 4 health), '4' king squid (80 x 120, 60 health). Waits 100-249 ticks
// (crouched for the last 40), then jumps in place for 90 ticks (king 110, up to 55 high, king
// 93.5) while its yaw eases round to a new direction: up to 120 degrees towards a random point,
// straight at a hero within 120, or 120-240 degrees round after a stuck walk. Then it walks 105
// ticks at 0.9 (king 1.08) along that direction; a walk that covered less than 15 makes it jump
// and turn round, otherwise it jumps again or waits, half and half.
class Squid : public XEnemyBase {
public:
    Squid(const XGameContext* g, int oid, const D3DVECTOR& p, SizeType size) : XEnemyBase(g, oid, p, size) {
        bool king = size == KING;
        static const uint16_t kSmall[5] = {0x162, 0x161, 0x162, 0x160, 0x175},
                              kKing[5] = {0x2ba, 0x2bb, 0x2ba, 0x2bc, 0x2bd};
        const uint16_t* f = king ? kKing : kSmall;
        model_ = std::make_unique<JObject3DX>(f[0]);
        for (int i = 1; i < 5; i++) model_->AppendFrame(f[i]);
        model_->SetCollisionModel(king ? 0x2be : 0x176);
        shadow_ = std::make_unique<XShadowSimple>(king ? 80.0f : 40.0f);
        collideBox_ = king ? std::make_unique<XCollideBox>(80.0f, 120.0f) : std::make_unique<XCollideBox>(40.0f, 60.0f);
        collideBox_->SetPos(startPos_);
        health_ = king ? 60 : 4;
        dir_ = UnitRand() * 6.28319f;
        yaw_ = dir_;
        UpdateWorldMatrix();
        chasable_ = true;
        liveState_ = XEnemyBase::NORMAL;
        moveState_ = WAIT;
    }
    void ProcessFrame() override {
        if (!game_->IsUpdatable(pos_)) return;
        XEnemyBase::ProcessFrame();
        if (liveState_ != XEnemyBase::NORMAL) return;
        if (moveState_ == WALK) Walk();
        else if (moveState_ == JUMP) Jump();
        else if (moveState_ == WAIT) Wait();
        UpdateWorldMatrix();
        CollisionCheck_Hero(game_->heroA);
        CollisionCheck_Hero(game_->heroB);
    }

private:
    enum MoveState { NON = 0, WALK = 1, JUMP = 2, WAIT = 3 };
    void Walk() {
        float speed = 0.9f;
        if (sizeType_ == KING) speed *= 1.2f;
        if (localCounter_ == 1) prevPosForWalk_ = pos_;
        frame_ = (int)((localCounter_ / 15 + 1) % 4);
        UpdatePosX(pos_.x + std::sin(dir_) * speed);
        UpdatePosZ(pos_.z + std::cos(dir_) * speed);
        if (localCounter_ < 105) return;
        localCounter_ = 0;
        if (ApproxDistance(pos_, prevPosForWalk_) < 15.0f) {
            turnAround_ = true;  // stuck: jump and turn round
            moveState_ = JUMP;
        } else {
            moveState_ = Random(2) == 0 ? JUMP : WAIT;
        }
    }
    void Jump() {
        unsigned frames = 90;
        float height = 55.0f;
        if (sizeType_ == KING) frames = 110, height *= 1.7f;
        if (localCounter_ == 1) {
            if (turnAround_) {
                dir_ = NormaliseAngle(dir_ + 0.0174533f * (UnitRand() * 120.0f + 120.0f));
            } else {
                dir_ = MakeAngleCloseTo_Constant(dir_, FindDirectionXZ(pos_, GenerateRandomPos()), 2.0944f);
                if (game_->DistanceToCloserHero(pos_) < 120.0f) dir_ = game_->DirToCloserHero(pos_);
            }
        }
        frame_ = localCounter_ < frames / 2 ? 0 : 4;
        UpdatePosY(floorLevel_ * 25.0f + std::sin((float)localCounter_ / (float)frames * kPi) * height);
        yaw_ = MakeAngleCloseTo(yaw_, dir_, sizeType_ == KING ? 0.03f : 0.05f, 0.0f, FLT_MAX);
        if (localCounter_ < frames) return;
        turnAround_ = false;
        localCounter_ = 0;
        moveState_ = WALK;
    }
    void Wait() {
        if (localCounter_ == 1) waitFrames_ = (unsigned)Random(150) + 100;
        if (localCounter_ > waitFrames_ - 40) frame_ = 4;  // crouching to jump
        if (localCounter_ < waitFrames_) return;
        localCounter_ = 0;
        moveState_ = JUMP;
    }
    MoveState moveState_ = NON;
    D3DVECTOR prevPosForWalk_{0, 0, 0};
    bool turnAround_ = false;
    unsigned waitFrames_ = 0;
};

// -------------------------------------------------------------------------- XEnemy_NeedleBall
// 'x' (X_AXIS, across the level) / 'y' (Z_AXIS, along it): a spiked ball (model 0x1f1, collision
// 0x1f5, a 40 x 40 box, shadow 38) rolling at 0.75 a tick and spinning 2.6 degrees a tick; when
// it hits something it reverses both. It can't be hurt, and moves even off camera (no
// IsUpdatable test). Its centre is 20 above the floor; the box stays at the floor (the original
// sets pos.y after placing the box).
class NeedleBall : public XEnemyBase {
public:
    enum Type { X_AXIS = 0, Z_AXIS = 1 };
    NeedleBall(const XGameContext* g, int oid, const D3DVECTOR& p, Type type) : XEnemyBase(g, oid, p, SMALL), type_(type) {
        model_ = std::make_unique<JObject3DX>(0x1f1);
        model_->SetCollisionModel(0x1f5);
        shadow_ = std::make_unique<XShadowSimple>(38.0f);
        collideBox_ = std::make_unique<XCollideBox>(40.0f, 40.0f);
        collideBox_->SetPos(startPos_);
        yaw_ = type_ == X_AXIS ? kHalfPi : 0.0f;
        pos_.y = startPos_.y + 20.0f;
        UpdateWorldMatrix();
        damagable_ = false;
        liveState_ = XEnemyBase::NORMAL;
    }
    void ProcessFrame() override {
        XEnemyBase::ProcessFrame();
        if (liveState_ != XEnemyBase::NORMAL) return;
        pitch_ = NormaliseAngle(pitch_ + rotSpeed_);
        bool hit = type_ == X_AXIS ? UpdatePosX(pos_.x + moveSpeed_) : UpdatePosZ(pos_.z + moveSpeed_);
        if (hit) {
            moveSpeed_ *= -1.0f;
            rotSpeed_ *= -1.0f;
        }
        UpdateWorldMatrix();
        CollisionCheck_Hero(game_->heroA);
        CollisionCheck_Hero(game_->heroB);
    }

private:
    Type type_;
    float moveSpeed_ = 0.75f, rotSpeed_ = 0.0453786f;
};

// ------------------------------------------------------------------------------ XEnemy_Barrel
// 'j': a barrel to shoot open (10 health, model 0x1f6, a 27 x 42.8 box that also stops heroes and
// enemies but not shots). It bursts at once: a medium coin, or a heart for every third barrel
// broken in the game (sound 0x267). It never hurts.
class BarrelEnemy : public XEnemyBase {
public:
    BarrelEnemy(const XGameContext* g, int oid, const D3DVECTOR& p) : XEnemyBase(g, oid, p, SMALL) {
        model_ = std::make_unique<JObject3DX>(0x1f6);
        model_->SetCollisionModel(0x204);
        shadow_ = std::make_unique<XShadowSimple>(37.0f);
        collideBox_ = std::make_unique<XCollideBox>(27.0f, 42.8f);
        collideBox_->SetPos(pos_);
        blocker_ = g->statics->CreateCollideBox(startPos_, 27.0f, 42.8f, 27.0f);
        if (blocker_) blocker_->SetBlockFire(false);
        UpdateWorldMatrix();
        sizeType_ = KING;
        health_ = 10;
        chasable_ = false;
        liveState_ = XEnemyBase::NORMAL;
    }
    void ProcessFrame() override {
        if (liveState_ == XEnemyBase::NON) return;
        localCounter_++;
        if (damageCounter_ > 0 && --damageCounter_ <= 0) model_->UseWhiteMaterial(false);
        if (liveState_ != XEnemyBase::DEAD) return;
        pos_.y += collideBox_->Height() * 0.5f;
        game_->particles->CreateEnemyExplode(pos_, false);
        if (blocker_) blocker_->SetEnabled(false);
        int n = game_->barrelCounter ? ++*game_->barrelCounter : 1;
        if (n % 3 == 0) {
            if (game_->createHeart) game_->createHeart(*this);
        } else if (game_->createEnemyCoin) {
            game_->createEnemyCoin(*this);
        }
        game_->playSound(0x267, pos_.z);
        liveState_ = XEnemyBase::NON;
    }

private:
    XStaticObjBase* blocker_ = nullptr;
};

// --------------------------------------------------------------------------- XEnemy_FenceDoor
// 'v' (across X) / 'w' (along Z, turned -90 degrees): a breakable fence panel, 40 x 82 x 10
// (5 health, model 0x1dc). Its box also stops heroes and enemies but not shots. It just
// bursts (sound 0x267), with no coin.
class FenceDoor : public XEnemyBase {
public:
    FenceDoor(const XGameContext* g, int oid, const D3DVECTOR& p, bool alongZ) : XEnemyBase(g, oid, p, SMALL) {
        model_ = std::make_unique<JObject3DX>(0x1dc);
        model_->SetCollisionModel(0x1dd);
        float wx = alongZ ? 10.0f : 40.0f, wz = alongZ ? 40.0f : 10.0f;
        if (alongZ) yaw_ = -kHalfPi;
        collideBox_ = std::make_unique<XCollideBox>(wx, 82.0f, wz);
        collideBox_->SetPos(startPos_);
        blocker_ = g->statics->CreateCollideBox(startPos_, wx, 82.0f, wz);
        if (blocker_) blocker_->SetBlockFire(false);
        UpdateWorldMatrix();
        health_ = 5;
        chasable_ = false;
        liveState_ = XEnemyBase::NORMAL;
    }
    void ProcessFrame() override {
        if (liveState_ == XEnemyBase::NON) return;
        localCounter_++;
        if (damageCounter_ > 0 && --damageCounter_ <= 0) model_->UseWhiteMaterial(false);
        if (liveState_ != XEnemyBase::DEAD) return;
        pos_.y += collideBox_->Height() * 0.5f;
        game_->particles->CreateEnemyExplode(pos_, false);
        if (blocker_) blocker_->SetEnabled(false);
        game_->playSound(0x267, pos_.z);
        liveState_ = XEnemyBase::NON;
    }

private:
    XStaticObjBase* blocker_ = nullptr;
};

// ---------------------------------------------------------------------------- XEnemy_NeedleFan
// 'm': a spiked bar (model 0x17f, 200 wide and 80 tall) turning on a post (model 0x17e, a 19 x 87
// box) at (0.6 + 0.1 x world) degrees a tick. It can't be hurt.
class NeedleFan : public XEnemyBase {
public:
    NeedleFan(const XGameContext* g, int oid, const D3DVECTOR& p) : XEnemyBase(g, oid, p, SMALL) {
        model_ = std::make_unique<JObject3DX>(0x17f);
        model_->SetCollisionModel(0x17d);
        shadow_ = std::make_unique<XShadowSimple>(42.0f);
        collideBox_ = std::make_unique<XCollideBox>(200.0f, 80.0f);
        collideBox_->SetPos(startPos_);
        fanBase_ = std::make_unique<JObject3DX>(0x17e);
        fanBase_->SetPos(startPos_);
        fanBase_->UpdateWorldMatrix();
        g->statics->CreateCollideBox(startPos_, 19.0f, 87.0f, 19.0f);
        yaw_ = 0;
        UpdateWorldMatrix();
        damagable_ = false;
        liveState_ = XEnemyBase::NORMAL;
    }
    void ProcessFrame() override {
        XEnemyBase::ProcessFrame();
        if (liveState_ != XEnemyBase::NORMAL) return;
        yaw_ = NormaliseAngle(yaw_ + (0.6f + game_->world * 0.1f) * 0.0174533f);
        UpdateWorldMatrix();
        CollisionCheck_Hero(game_->heroA);
        CollisionCheck_Hero(game_->heroB);
    }
    void Render(Device* d) override {
        if (liveState_ == XEnemyBase::NON || !game_->IsVisible(pos_)) return;
        model_->Render(d);
        fanBase_->Render(d);
        shadow_->SetPosXZ(startPos_);
        shadow_->Render(d);
    }

private:
    std::unique_ptr<JObject3DX> fanBase_;
};

// -------------------------------------------------------------------------------- XEnemy_Kiwi
// 'f': a kiwi (frames 0x1ad 0x1b4 0x1ad 0x1af, collision 0x1b5, a 35 x 35 box, shadow 35, 4
// health). It waits 100-200 ticks and walks 127-tick legs at 0.5 (turning at most 0.8 degrees a
// tick) towards a random point, or behind itself after a stuck leg; after a leg it walks on, or
// one time in three waits. When a hero comes within 80, or when it is shot, it runs away for 95
// ticks at 1.3 (turning up to 3 degrees a tick); a run that leaves it stuck makes it run at the
// hero instead next time. It keeps running while a hero is within 80.
class Kiwi : public XEnemyBase {
public:
    Kiwi(const XGameContext* g, int oid, const D3DVECTOR& p) : XEnemyBase(g, oid, p, SMALL) {
        static const uint16_t kFrames[4] = {0x1ad, 0x1b4, 0x1ad, 0x1af};
        model_ = std::make_unique<JObject3DX>(kFrames[0]);
        for (int i = 1; i < 4; i++) model_->AppendFrame(kFrames[i]);
        model_->SetCollisionModel(0x1b5);
        shadow_ = std::make_unique<XShadowSimple>(35.0f);
        collideBox_ = std::make_unique<XCollideBox>(35.0f, 35.0f);
        collideBox_->SetPos(startPos_);
        dir_ = UnitRand() * 6.28319f;
        yaw_ = dir_;
        UpdateWorldMatrix();
        health_ = 4;
        chasable_ = true;
        liveState_ = XEnemyBase::NORMAL;
        moveState_ = WAIT;
    }
    void ProcessFrame() override {
        if (!game_->IsUpdatable(pos_)) return;
        XEnemyBase::ProcessFrame();
        if (liveState_ != XEnemyBase::NORMAL) return;
        if (moveState_ == WAIT) Wait();
        else if (moveState_ == WALK) Walk();
        else if (moveState_ == RUN) Run();
        UpdateWorldMatrix();
        CollisionCheck_Hero(game_->heroA);
        CollisionCheck_Hero(game_->heroB);
    }

protected:
    // XEnemy_Kiwi::_Damage: hurt as usual, then it runs
    void Damage(const D3DVECTOR& heroPos) override {
        XEnemyBase::Damage(heroPos);
        Next(RUN);
    }

private:
    enum MoveState { NON = 0, WAIT = 1, WALK = 2, RUN = 3 };
    void Next(MoveState s) {
        localCounter_ = 0;
        moveState_ = s;
    }
    bool IsHeroClose() const { return game_->DistanceToCloserHero(pos_) < 80.0f; }
    void Step(float speed) {
        yaw_ = dir_;
        UpdatePosX(pos_.x + std::sin(dir_) * speed);
        UpdatePosZ(pos_.z + std::cos(dir_) * speed);
    }
    void Wait() {
        if (localCounter_ == 1) {
            frame_ = 0;
            waitFrames_ = (unsigned)RandomI(100, 200, true);
        }
        if (IsHeroClose()) Next(RUN);
        else if (localCounter_ >= waitFrames_) Next(WALK);
    }
    void Walk() {
        if (localCounter_ == 1) {
            prevPos_ = pos_;
            targetPos_ = turnAround_ ? GenerateBehindPos() : GenerateRandomPos();
        }
        frame_ = (int)((localCounter_ / 16 + 1) % 4);
        dir_ = MakeAngleCloseTo_Constant(dir_, FindDirectionXZ(pos_, targetPos_), 0.0139626f);
        Step(0.5f);
        if (IsHeroClose()) {
            Next(RUN);
        } else if (localCounter_ >= 127) {
            turnAround_ = ApproxDistance(pos_, prevPos_) < 10.0f;
            if (turnAround_) Next(WALK);
            else Next(Random(3) == 0 ? WAIT : WALK);
        }
    }
    void Run() {
        if (localCounter_ == 1) prevPos_ = pos_;
        frame_ = (int)((localCounter_ / 6 + 1) % 4);
        float toHero = game_->DirToCloserHero(pos_);
        dir_ = MakeAngleCloseTo_Constant(dir_, turnAround_ ? toHero : toHero + kPi, 0.0523599f);
        Step(1.3f);
        if (localCounter_ < 95) return;
        turnAround_ = !turnAround_ && ApproxDistance(pos_, prevPos_) < 10.0f;  // stuck: run at the hero
        Next(turnAround_ || IsHeroClose() ? RUN : WAIT);
    }
    MoveState moveState_ = NON;
    unsigned waitFrames_ = 0;
    bool turnAround_ = false;
    D3DVECTOR targetPos_{0, 0, 0}, prevPos_{0, 0, 0};
};

// ------------------------------------------------------------------------------ XEnemy_Turtle
// 'e': a turtle (frames 0x1b0 0x1b1 0x1b0 0x1b2 0x1b3 at 0.9 scale, collision 0x1b6, a 30.6 x 24.3
// box, shadow 34, 99999 health). It walks for ever at 0.4 a tick, turning at most 0.8 degrees a
// tick towards a random point (or behind itself after a stuck 150-tick leg), or towards a hero
// within 120. A shot never hurts it: it hides in its shell (frame 4) for 600 ticks, starting
// over with each hit, then waits 50 and walks on.
class Turtle : public XEnemyBase {
public:
    Turtle(const XGameContext* g, int oid, const D3DVECTOR& p) : XEnemyBase(g, oid, p, SMALL) {
        static const uint16_t kFrames[5] = {0x1b0, 0x1b1, 0x1b0, 0x1b2, 0x1b3};
        model_ = std::make_unique<JObject3DX>(kFrames[0]);
        for (int i = 1; i < 5; i++) model_->AppendFrame(kFrames[i]);
        model_->SetCollisionModel(0x1b6);
        shadow_ = std::make_unique<XShadowSimple>(34.0f);
        collideBox_ = std::make_unique<XCollideBox>(30.6f, 24.3f);
        collideBox_->SetPos(startPos_);
        dir_ = UnitRand() * 6.28319f;
        yaw_ = dir_;
        model_->SetScale(0.9f);
        UpdateWorldMatrix();
        health_ = 99999;
        chasable_ = true;
        liveState_ = XEnemyBase::NORMAL;
        moveState_ = WALK;
    }
    void ProcessFrame() override {
        if (!game_->IsUpdatable(pos_)) return;
        XEnemyBase::ProcessFrame();
        if (liveState_ != XEnemyBase::NORMAL) return;
        if (moveState_ == WALK) Walk();
        else if (moveState_ == WAIT) Wait();
        else if (moveState_ == SHIELD) Shield();
        UpdateWorldMatrix();
        CollisionCheck_Hero(game_->heroA);
        CollisionCheck_Hero(game_->heroB);
    }

protected:
    // XEnemy_Turtle::_Damage: no sound, flash or health lost; into the shell (again)
    void Damage(const D3DVECTOR&) override {
        localCounter_ = 0;
        moveState_ = SHIELD;
    }

private:
    enum MoveState { NON = 0, WALK = 1, WAIT = 2, SHIELD = 3 };
    void Walk() {
        if (localCounter_ == 1) {
            prevPosForWalk_ = pos_;
            targetPos_ = turnAround_ ? GenerateBehindPos() : GenerateRandomPos();
        }
        walkCounter_++;
        frame_ = (int)((walkCounter_ / 10 + 1) % 4);
        if (!turnAround_ && game_->DistanceToCloserHero(pos_) < 120.0f)
            dir_ = MakeAngleCloseTo_Constant(dir_, game_->DirToCloserHero(pos_), 0.0139626f);
        else
            dir_ = MakeAngleCloseTo_Constant(dir_, FindDirectionXZ(pos_, targetPos_), 0.0139626f);
        yaw_ = dir_;
        UpdatePosX(pos_.x + std::sin(dir_) * 0.4f);
        UpdatePosZ(pos_.z + std::cos(dir_) * 0.4f);
        if (localCounter_ < 150) return;
        turnAround_ = ApproxDistance(pos_, prevPosForWalk_) < 10.0f;  // stuck: turn round next leg
        localCounter_ = 0;
    }
    void Wait() {
        if (localCounter_ == 1) frame_ = 0;
        if (localCounter_ < 50) return;
        walkCounter_ = 0;
        localCounter_ = 0;
        moveState_ = WALK;
    }
    void Shield() {
        if (localCounter_ == 1) frame_ = 4;
        if (localCounter_ < 600) return;
        localCounter_ = 0;
        moveState_ = WAIT;
    }
    MoveState moveState_ = NON;
    unsigned walkCounter_ = 0;
    bool turnAround_ = false;
    D3DVECTOR targetPos_{0, 0, 0}, prevPosForWalk_{0, 0, 0};
};

// -------------------------------------------------------------------------------- XEnemy_Fan
// 'q' 'r' 's' (directions 4, 0, 2 in eighths of a turn: blowing along -Z, +Z and +X): a fan
// (model 0x1ca, collision 0x1c9, a 70 x 85 box, shadow 32) 10.8 out from its post (model 0x1cb,
// a static 20 x 52 box) and 44.5 up, its blades turning 9 degrees a tick. It can't be hurt and
// doesn't hurt a hero who is still flashing. Each tick it pushes a NORMAL hero whose centre is
// within 56 of its axis (across and up) and up to 320 in front: full strength within 30 of the
// axis, fading to nothing at 56, and falling off with cos(distance / 320 x 90 degrees). The push
// is 1.0 along X, 1.2 along Z. No IsUpdatable test.
class Fan : public XEnemyBase {
public:
    Fan(const XGameContext* g, int oid, const D3DVECTOR& p, int dir) : XEnemyBase(g, oid, p, SMALL), dir_(dir) {
        model_ = std::make_unique<JObject3DX>(0x1ca);
        model_->SetCollisionModel(0x1c9);
        shadow_ = std::make_unique<XShadowSimple>(32.0f);
        collideBox_ = std::make_unique<XCollideBox>(70.0f, 85.0f);
        collideBox_->SetPos(startPos_);
        yaw_ = dir_ * kQuarterPi;
        pos_.x += std::sin(dir_ * kQuarterPi) * 10.8f;
        pos_.z += std::cos(dir_ * kQuarterPi) * 10.8f;
        pos_.y += 44.5f;
        UpdateWorldMatrix();
        fanBase_ = std::make_unique<JObject3DX>(0x1cb);
        fanBase_->SetPos(startPos_);
        fanBase_->SetYaw(dir_ * kQuarterPi);
        fanBase_->UpdateWorldMatrix();
        g->statics->CreateCollideBox(startPos_, 20.0f, 52.0f, 20.0f);
        noCollisionWhenHeroDamaged_ = true;
        damagable_ = false;
        liveState_ = XEnemyBase::NORMAL;
        moveState_ = ROTATE;
    }
    void ProcessFrame() override {
        XEnemyBase::ProcessFrame();
        if (liveState_ != XEnemyBase::NORMAL) return;
        if (moveState_ == ROTATE) {
            roll_ = NormaliseAngle(roll_ - 0.15708f);
            PushHero(game_->heroA);
            PushHero(game_->heroB);
        }
        UpdateWorldMatrix();
        CollisionCheck_Hero(game_->heroA);
        CollisionCheck_Hero(game_->heroB);
    }
    void Render(Device* d) override {
        if (liveState_ == XEnemyBase::NON || !game_->IsVisible(pos_)) return;
        model_->Render(d);
        fanBase_->Render(d);
        shadow_->SetPosXZ(startPos_);
        shadow_->Render(d);
    }

private:
    enum MoveState { NON = 0, ROTATE = 1 };
    // XEnemy_Fan::_PushHero
    void PushHero(XHero* hero) {
        if (!hero || hero->GetLiveState() != XHero::NORMAL) return;
        const float kInner = 30.0f, kOuter = 56.0f, kRange = 320.0f;
        const D3DVECTOR& hp = hero->GetPos();
        D3DVECTOR c(hp.x, hp.y + 14.5f, hp.z);  // XHeroBase::GetCenterPos
        // strength across the stream: ExactDistanceZY (blowing along X) or XY (along Z)
        auto across = [&](float d) { return d < kInner ? 1.0f : d < kOuter ? 1.0f - (d - kInner) / 26.0f : 0.0f; };
        auto along = [&](float d) { return std::cos(std::min(std::fabs(d), kRange) / kRange * kHalfPi); };
        if (dir_ == 2) {
            if (std::fabs(pos_.z - c.z) > kOuter || std::fabs(pos_.y - c.y) > kOuter) return;
            if (pos_.x > c.x || c.x > pos_.x + kRange) return;
            float dz = c.z - pos_.z, dy = c.y - pos_.y;
            hero->IncreasePushX(1.0f * along(pos_.x - c.x) * across(std::sqrt(dz * dz + dy * dy)));
        } else if (dir_ == 0 || dir_ == 4) {
            if (std::fabs(pos_.x - c.x) > kOuter || std::fabs(pos_.y - c.y) > kOuter) return;
            if (dir_ == 0 ? (pos_.z > c.z || c.z > pos_.z + kRange) : (pos_.z - kRange > c.z || c.z > pos_.z)) return;
            float dx = c.x - pos_.x, dy = c.y - pos_.y;
            hero->IncreasePushZ((dir_ == 0 ? 1.2f : -1.2f) * along(pos_.z - c.z) * across(std::sqrt(dx * dx + dy * dy)));
        }
    }
    int dir_;
    MoveState moveState_ = NON;
    std::unique_ptr<JObject3DX> fanBase_;
};

// ---------------------------------------------------------------------------- XEnemy_FloorFan
// 't': a fan lying in the floor (model 0x1cd, collision 0x1cc, an 80 x 20 box, shadow 28) over its
// housing (model 0x1ce, a static 16 x 16 box), its blades turning 10 degrees a tick about the
// vertical. It can't be hurt and doesn't hurt a hero who is still flashing. Each tick it lifts a
// NORMAL hero whose feet are within 58 of it along X and Z and at least 10 above it: full strength
// within 30 (straight-line distance across the floor), fading to nothing at 58, and falling off
// with cos(height / 320 x 90 degrees). The lift breathes, 4.8 +- 0.8 over 120 ticks, and marks
// the hero as pushed by a fan. No IsUpdatable test.
class FloorFan : public XEnemyBase {
public:
    FloorFan(const XGameContext* g, int oid, const D3DVECTOR& p) : XEnemyBase(g, oid, p, SMALL) {
        model_ = std::make_unique<JObject3DX>(0x1cd);
        model_->SetCollisionModel(0x1cc);
        UpdateWorldMatrix();
        shadow_ = std::make_unique<XShadowSimple>(28.0f);
        collideBox_ = std::make_unique<XCollideBox>(80.0f, 20.0f);
        collideBox_->SetPos(startPos_);
        fanBase_ = std::make_unique<JObject3DX>(0x1ce);
        fanBase_->SetPos(startPos_);
        fanBase_->UpdateWorldMatrix();
        g->statics->CreateCollideBox(startPos_, 16.0f, 16.0f, 16.0f);
        noCollisionWhenHeroDamaged_ = true;
        damagable_ = false;
        liveState_ = XEnemyBase::NORMAL;
        moveState_ = ROTATE;
    }
    void ProcessFrame() override {
        XEnemyBase::ProcessFrame();
        if (liveState_ != XEnemyBase::NORMAL) return;
        if (moveState_ == ROTATE) Rotate();
        UpdateWorldMatrix();
        CollisionCheck_Hero(game_->heroA);
        CollisionCheck_Hero(game_->heroB);
    }
    void Render(Device* d) override {
        if (liveState_ == XEnemyBase::NON || !game_->IsVisible(pos_)) return;
        model_->Render(d);
        fanBase_->Render(d);
        shadow_->SetPosXZ(startPos_);
        shadow_->Render(d);
    }

private:
    enum MoveState { NON = 0, ROTATE = 1 };
    // XEnemy_FloorFan::_ProcessFrame_Rotate
    void Rotate() {
        yaw_ = NormaliseAngle(yaw_ - 0.174533f);
        PushHero(game_->heroA);
        PushHero(game_->heroB);
    }
    // XEnemy_FloorFan::_PushHero
    void PushHero(XHero* hero) {
        if (liveState_ != XEnemyBase::NORMAL) return;
        if (!hero || hero->GetLiveState() != XHero::NORMAL) return;
        const float kInner = 30.0f, kOuter = 58.0f, kRange = 320.0f;
        const D3DVECTOR& hp = hero->GetPos();
        if (std::fabs(pos_.z - hp.z) > kOuter) return;
        if (std::fabs(pos_.x - hp.x) > kOuter) return;
        if (pos_.y + 10.0f > hp.y) return;
        float height = std::min(std::fabs(pos_.y - hp.y), kRange);
        float along = std::cos(height / kRange * kHalfPi);
        float dx = hp.x - pos_.x, dz = hp.z - pos_.z;
        float d = std::sqrt(dx * dx + dz * dz);  // ExactDistanceXZ
        float across = 0;
        if (d < kInner) across = 1.0f;
        else if (d < kOuter) across = 1.0f - (d - kInner) / 28.0f;
        float wave = std::sin(static_cast<float>(localCounter_ % 120) / 120.0f * 6.28319f);
        float push = (wave * 0.8f + 4.8f) * along * across;
        if (push > 0) {
            hero->IncreasePushY(push);
            hero->SetPushedByFan();
        }
    }
    MoveState moveState_ = NON;
    std::unique_ptr<JObject3DX> fanBase_;
};

// ------------------------------------------------------------------------------ XEnemy_Ghost
// 'g': a ghost (models 0x1de / 0x1df, collision 0x1e0, scaled 0.8, a 27.2 x 36 box, an XShadow
// 27.2 that lies on the floors below) floating 80 above its floor and passing through everything.
// It can't be killed: a shot stops on it and makes it flinch. WALK drifts at 0.4 for 120 ticks at
// a time towards a random point (round behind itself after being stuck), turning 0.6 degrees a
// tick and bobbing 5 over 120 ticks; once its attack timer (600..1000 ticks at the start) has run
// out and a hero comes within 120, WARNING turns it to face the hero for 80 ticks, then ATTACK
// (sound 0x2b4, mouth open) swoops at 0.75 straight towards the hero for 160 ticks, dipping from
// height 80 to the floor and back. FLINCH (a shot) pushes it away from the shooter at 2.8, slowing
// by 0.07, while it rises back to height 80. Heights in ATTACK and FLINCH are absolute, not above
// its own floor, as the original.
class Ghost : public XEnemyBase {
public:
    Ghost(const XGameContext* g, int oid, const D3DVECTOR& p) : XEnemyBase(g, oid, p, SMALL) {
        model_ = std::make_unique<JObject3DX>(0x1de);
        model_->AppendFrame(0x1df);
        model_->SetCollisionModel(0x1e0);
        ghostShadow_ = std::make_unique<XShadow>(27.2f);
        collideBox_ = std::make_unique<XCollideBox>(27.2f, 36.0f);
        collideBox_->SetPos(startPos_);
        dir_ = UnitRand() * 6.28319f;
        yaw_ = dir_;
        ghostPos_ = pos_;
        ghostPos_.y += 80.0f;
        SetPos(ghostPos_);
        model_->SetScale(0.8f);
        UpdateWorldMatrix();
        offsetCounter_ = (unsigned)Random(120);
        startAttackTimer_ = (unsigned)RandomI(600, 1000, true);
        health_ = 99999;
        chasable_ = true;
        collidable_ = false;
        liveState_ = XEnemyBase::NORMAL;
        moveState_ = WALK;
    }
    void ProcessFrame() override {
        if (!game_->IsUpdatable(pos_)) return;
        XEnemyBase::ProcessFrame();
        if (liveState_ != XEnemyBase::NORMAL) return;
        if (moveState_ == WALK) Walk();
        else if (moveState_ == WARNING) Warning();
        else if (moveState_ == ATTACK) Attack();
        else if (moveState_ == FLINCH) Flinch();
        if (moveState_ == WALK) {
            offsetCounter_++;
            offsetY_ = std::sin((float)(offsetCounter_ % 120) / 120.0f * 6.28319f) * 5.0f;
        }
        SetPosX(ghostPos_.x);
        SetPosZ(ghostPos_.z);
        SetPosY(ghostPos_.y + offsetY_);
        model_->SetScale(0.8f);
        UpdateWorldMatrix();
        CollisionCheck_Hero(game_->heroA);
        CollisionCheck_Hero(game_->heroB);
    }
    void Render(Device* d) override {
        if (liveState_ == XEnemyBase::NON || !game_->IsVisible(pos_)) return;
        ghostShadow_->SetPos(pos_ + model_->BlendOffset());  // (native: where the model is drawn)
        ghostShadow_->Render(d, game_->level->Collision());
        model_->SetCurrentFrame(frame_);
        model_->Render(d);
        // (the original also calls the box's debug draw)
    }

protected:
    // XEnemy_Ghost::_Damage: no sound, no flash, no health lost; it flinches away from the shooter
    void Damage(const D3DVECTOR& heroPos) override {
        damagePos_ = heroPos;
        localCounter_ = 0;
        moveState_ = FLINCH;
    }

private:
    enum MoveState { NON = 0, WALK = 1, WARNING = 2, ATTACK = 3, FLINCH = 4 };
    void SetPos(const D3DVECTOR& p) {  // XEnemyBase::_SetPos
        pos_ = p;
        collideBox_->SetPos(p);
    }
    // _UpdatePosX_Ghost / _UpdatePosZ_Ghost: no collision at all
    void UpdatePosX_Ghost(float x) { ghostPos_.x = x; }
    void UpdatePosZ_Ghost(float z) { ghostPos_.z = z; }
    // XEnemy_Ghost::_ProcessFrame_Walk
    void Walk() {
        if (localCounter_ == 1) {
            dir_ = yaw_;
            frame_ = 0;
            prevPosForWalk_ = ghostPos_;
            targetPos_ = turnAround_ ? GenerateBehindPos() : GenerateRandomPos();
        }
        dir_ = MakeAngleCloseTo_Constant(dir_, FindDirectionXZ(ghostPos_, targetPos_), 0.010472f);
        yaw_ = dir_;
        UpdatePosX_Ghost(ghostPos_.x + std::sin(dir_) * 0.4f);
        UpdatePosZ_Ghost(ghostPos_.z + std::cos(dir_) * 0.4f);
        if (startAttackTimer_ > 0) startAttackTimer_--;
        if (!turnAround_ && startAttackTimer_ == 0 && game_->DistanceToCloserHero(pos_) < 120.0f) {
            localCounter_ = 0;
            moveState_ = WARNING;
        } else if (localCounter_ >= 120) {
            // (pos includes the bob; prevPosForWalk does not)
            turnAround_ = ApproxDistance(pos_, prevPosForWalk_) < 10.0f;
            localCounter_ = 0;
        }
    }
    // XEnemy_Ghost::_ProcessFrame_Warning
    void Warning() {
        XHero* hero = game_->GetCloserHero(pos_);
        dir_ = FindDirectionXZ(ghostPos_, hero->GetPos());
        yaw_ = MakeAngleCloseTo(yaw_, dir_, 0.08f, 0.0f, kFloatMax);
        if (localCounter_ >= 80) {
            localCounter_ = 0;
            moveState_ = ATTACK;
        }
    }
    // XEnemy_Ghost::_ProcessFrame_Attack
    void Attack() {
        if (localCounter_ == 1) {
            game_->playSound(0x2b4, pos_.z);
            frame_ = 1;
            XHero* hero = game_->GetCloserHero(pos_);
            dir_ = FindDirectionXZ(ghostPos_, hero->GetPos());
        }
        UpdatePosX_Ghost(ghostPos_.x + std::sin(dir_) * 0.75f);
        UpdatePosZ_Ghost(ghostPos_.z + std::cos(dir_) * 0.75f);
        ghostPos_.y = (1.0f - std::sin((float)localCounter_ / 160.0f * 3.14159f)) * 80.0f;
        yaw_ = MakeAngleCloseTo(yaw_, dir_, 0.08f, 0.0f, kFloatMax);
        if (localCounter_ >= 160) {
            startAttackTimer_ = (unsigned)RandomI(600, 800, true);
            localCounter_ = 0;
            moveState_ = WALK;
        }
    }
    // XEnemy_Ghost::_ProcessFrame_Flinch
    void Flinch() {
        if (localCounter_ == 1) {
            frame_ = 1;
            flinchSpeed_ = 2.8f;
            dir_ = FindDirectionXZ(ghostPos_, damagePos_) + 3.14159f;
        }
        flinchSpeed_ -= 0.07f;
        if (flinchSpeed_ < 0.0f) flinchSpeed_ = 0.0f;
        UpdatePosX_Ghost(ghostPos_.x + std::sin(dir_) * flinchSpeed_);
        UpdatePosZ_Ghost(ghostPos_.z + std::cos(dir_) * flinchSpeed_);
        ghostPos_.y = MakeValueCloseTo(ghostPos_.y, 80.0f, 0.1f, 0.5f, kFloatMax);
        yaw_ = MakeAngleCloseTo(yaw_, dir_ + 3.14159f, 0.08f, 0.0f, kFloatMax);
        if (localCounter_ >= 40 && ghostPos_.y == 80.0f) {
            startAttackTimer_ = (unsigned)RandomI(400, 600, true);
            localCounter_ = 0;
            moveState_ = WALK;
        }
    }
    std::unique_ptr<XShadow> ghostShadow_;
    MoveState moveState_ = NON;
    D3DVECTOR ghostPos_{0, 0, 0}, damagePos_{0, 0, 0};
    unsigned offsetCounter_ = 0, startAttackTimer_ = 0;
    bool turnAround_ = false;
    float offsetY_ = 0;
    D3DVECTOR targetPos_{0, 0, 0}, prevPosForWalk_{0, 0, 0};
    float flinchSpeed_ = 0;
};

// ------------------------------------------------------------------------------ XEnemy_Hammer
// 'k': a hammer (model 0x17b, collision 0x17c, a 180 x 120 box) on a base (model 0x17a, static
// boxes 60 x 7 and 35 x 35). It can't be hurt. When a hero comes within 160 it rises 30 over 60
// ticks and tracks the closer hero (1.5 degrees a tick); within 100 and lined up (6 degrees) it
// swings down in front over 50 ticks (sound 0x277 at 20, the thud 0x276, camera shake 2.4 and a
// dust ring 60 ahead), lies there 150 ticks and lifts back over 120. If the hero gets behind it
// (12 degrees) while it lies there, it swings over backwards the same way (80 ticks). Out of
// range it sinks back and waits. No visibility or IsUpdatable test.
class Hammer : public XEnemyBase {
public:
    Hammer(const XGameContext* g, int oid, const D3DVECTOR& p) : XEnemyBase(g, oid, p, SMALL) {
        model_ = std::make_unique<JObject3DX>(0x17b);
        model_->SetCollisionModel(0x17c);
        collideBox_ = std::make_unique<XCollideBox>(180.0f, 120.0f);
        collideBox_->SetPos(startPos_);
        hammerBase_ = std::make_unique<JObject3DX>(0x17a);
        hammerBase_->SetPos(startPos_);
        hammerBase_->UpdateWorldMatrix();
        g->statics->CreateCollideBox(startPos_, 60.0f, 7.0f, 60.0f);
        g->statics->CreateCollideBox(startPos_, 35.0f, 35.0f, 35.0f);
        yaw_ = kHalfPi;
        UpdateWorldMatrix();
        damagable_ = false;
        liveState_ = XEnemyBase::NORMAL;
        moveState_ = WAIT;
    }
    void ProcessFrame() override {
        XEnemyBase::ProcessFrame();
        if (liveState_ != XEnemyBase::NORMAL) return;
        switch (moveState_) {
            case WAIT: Wait(); break;
            case MOVEUP: MoveUp(); break;
            case MOVEDOWN: MoveDown(); break;
            case ALERT: Alert(); break;
            case ATTACK_START: AttackStart(); break;
            case ATTACK_WAIT: AttackWait(); break;
            case ATTACK_END: AttackEnd(); break;
            case BEHIND_START: BehindStart(); break;
            case BEHIND_WAIT: BehindWait(); break;
            case BEHIND_END: BehindEnd(); break;
            default: break;
        }
        UpdateWorldMatrix();
        hammerBase_->SetYaw(yaw_);
        hammerBase_->UpdateWorldMatrix();
        CollisionCheck_Hero(game_->heroA);
        CollisionCheck_Hero(game_->heroB);
    }
    void Render(Device* d) override {
        if (liveState_ == XEnemyBase::NON || !game_->IsVisible(pos_)) return;
        model_->Render(d);
        hammerBase_->Render(d);
    }

private:
    enum MoveState {
        NON = 0, WAIT, MOVEUP, MOVEDOWN, ALERT, ATTACK_START, ATTACK_WAIT, ATTACK_END,
        BEHIND_START, BEHIND_WAIT, BEHIND_END
    };
    void Next(MoveState s) {
        localCounter_ = 0;
        moveState_ = s;
    }
    void Wait() {
        if (game_->DistanceToCloserHero(pos_) < 160.0f) Next(MOVEUP);
    }
    // the hammer itself moves (its box stays put)
    void MoveUp() {
        pos_.y += 0.5f;
        if (localCounter_ >= 60) Next(ALERT);
    }
    void MoveDown() {
        pos_.y -= 0.5f;
        if (localCounter_ >= 60) Next(WAIT);
    }
    void Alert() {
        float dir = game_->DirToCloserHero(pos_);
        yaw_ = MakeAngleCloseTo_Constant(yaw_, dir, 0.0261799f);
        float dist = game_->DistanceToCloserHero(pos_);
        if (dist > 160.0f) Next(MOVEDOWN);
        else if (dist < 100.0f && AngleDifference(yaw_, dir) < 0.10472f) Next(ATTACK_START);
    }
    // the thud: sound, shake and a dust ring 60 from the base along `side` (1 ahead, -1 behind)
    void Slam(float side) {
        game_->playSound(0x276, pos_.z);
        game_->startShaking(2.4f);
        D3DVECTOR at(startPos_.x + side * std::sin(yaw_) * 60.0f, startPos_.y + 0.5f,
                     startPos_.z + side * std::cos(yaw_) * 60.0f);
        game_->particles->CreateHammerEffect(at);
    }
    void AttackStart() {
        pitch_ = (1.0f - std::cos((float)localCounter_ / 50.0f * kHalfPi)) * kHalfPi;
        if (localCounter_ == 20) game_->playSound(0x277, pos_.z);
        if (localCounter_ < 50) return;
        Slam(1.0f);
        Next(ATTACK_WAIT);
    }
    void AttackWait() {
        if (AngleDifference(yaw_ + kPi, game_->DirToCloserHero(pos_)) < 0.20944f) Next(BEHIND_START);
        else if (localCounter_ >= 150) Next(ATTACK_END);
    }
    void AttackEnd() {
        pitch_ = (1.0f - (float)localCounter_ / 120.0f) * kHalfPi;
        if (localCounter_ >= 120) Next(ALERT);
    }
    void BehindStart() {
        pitch_ = std::cos((float)localCounter_ / 80.0f * kHalfPi) * kPi - kHalfPi;
        if (localCounter_ == 20) game_->playSound(0x277, pos_.z);
        if (localCounter_ < 80) return;
        Slam(-1.0f);
        Next(BEHIND_WAIT);
    }
    void BehindWait() {
        if (localCounter_ >= 150) Next(BEHIND_END);
    }
    void BehindEnd() {
        pitch_ = (1.0f - (float)localCounter_ / 120.0f) * -kHalfPi;
        if (localCounter_ >= 120) Next(ALERT);
    }
    MoveState moveState_ = NON;
    std::unique_ptr<JObject3DX> hammerBase_;
};

// -------------------------------------------------------------------------------- XEnemy_Fish
// 'i': a fish swimming round a circle of radius 80 about (100, -62, cell z) under the water, a
// half turn at a time (1 degree a tick, the way picked at random), and after a few half turns
// (2 to 5 for type A, 1 to 2 for type B) jumping across the circle from one side to the other
// (x 20 to 180, 92 high at the top), with a wave each time it crosses the surface. Type A
// (model 0x203) lives in worlds 1-2, type B (0x2af) in 3-4. It can't be hurt or stopped; its
// 200 x 120 box stays at the centre, so the mesh decides what it touches.
class Fish : public XEnemyBase {
public:
    enum MoveState { NON = 0, SWIM1 = 1, SWIM2 = 2, JUMP1 = 3, JUMP2 = 4 };
    Fish(const XGameContext* g, int oid, const D3DVECTOR& p, bool typeA) : XEnemyBase(g, oid, p, SMALL), typeA_(typeA) {
        static const uint16_t kFramesA[5] = {0x203, 0x200, 0x203, 0x201, 0x202};
        static const uint16_t kFramesB[5] = {0x2af, 0x2b0, 0x2af, 0x2b1, 0x2b2};
        const uint16_t* f = typeA_ ? kFramesA : kFramesB;
        model_ = std::make_unique<JObject3DX>(f[0]);
        for (int i = 1; i < 5; i++) model_->AppendFrame(f[i]);
        model_->SetCollisionModel(0x1ff);
        startPos_.x = 100.0f;
        startPos_.y = -62.0f;
        collideBox_ = std::make_unique<XCollideBox>(200.0f, 120.0f);
        collideBox_->SetPos(startPos_);
        posAngle_ = 0;
        pos_ = GetPosAtAngle(posAngle_);
        UpdateWorldMatrix();
        clockwise_ = Random(2) == 0;
        jumpReadyTimer_ = typeA_ ? (int)RandomI(2, 5, true) : (int)RandomI(1, 2, true);
        damagable_ = false;
        chasable_ = false;
        collidable_ = false;
        liveState_ = XEnemyBase::NORMAL;
        moveState_ = SWIM1;
    }
    void ProcessFrame() override {
        if (!game_->IsUpdatable(pos_)) return;
        XEnemyBase::ProcessFrame();
        if (liveState_ != XEnemyBase::NORMAL) return;
        // one state a tick (a new state starts on the next)
        if (moveState_ == SWIM1) Swim(0.0f, kPi, -kPi, SWIM2, JUMP2);
        else if (moveState_ == SWIM2) Swim(kPi, 2.0f * kPi, 0.0f, SWIM1, JUMP1);
        else if (moveState_ == JUMP1 || moveState_ == JUMP2) Jump();
        UpdateWorldMatrix();
        CollisionCheck_Hero(game_->heroA);
        CollisionCheck_Hero(game_->heroB);
    }

private:
    static constexpr float kPi = 3.14159f;

    // XEnemy_Fish::GetPosAtAngle: on the circle, angle 0 at the back (x 20), turning towards +Z
    D3DVECTOR GetPosAtAngle(float a) const {
        a -= 1.5708f;
        return D3DVECTOR(std::sin(a) * 80.0f + startPos_.x, startPos_.y, std::cos(a) * 80.0f + startPos_.z);
    }
    // _ProcessFrame_Swim1 (0 to +-180 degrees) and _ProcessFrame_Swim2 (180 to 360 or 0): half a
    // turn, then the next half turn or, when the countdown runs out, a jump
    void Swim(float from, float clockwiseEnd, float otherEnd, MoveState next, MoveState jump) {
        if (localCounter_ == 1) posAngle_ = from;
        if (clockwise_) {
            posAngle_ += 0.0174533f;
            if (posAngle_ >= clockwiseEnd) posAngle_ = clockwiseEnd;
        } else {
            posAngle_ -= 0.0174533f;
            if (otherEnd >= posAngle_) posAngle_ = otherEnd;
        }
        const D3DVECTOR prev = pos_;
        pos_ = GetPosAtAngle(posAngle_);
        yaw_ = MakeAngleCloseTo(yaw_, FindDirectionXZ(prev, pos_), 0.1f, 0.0f, FLT_MAX);
        pitch_ = MakeAngleCloseTo(pitch_, 0.0f, 0.1f, 0.0f, FLT_MAX);
        walkCounter_++;
        frame_ = (int)((walkCounter_ / 12) % 4);
        if (posAngle_ >= clockwiseEnd || otherEnd >= posAngle_) {
            localCounter_ = 0;
            moveState_ = --jumpReadyTimer_ <= 0 ? jump : next;
        }
    }
    // _ProcessFrame_Jump: JUMP1 from x 20 to 180, JUMP2 back, 0.008 of the way a tick, arcing 92 up
    void Jump() {
        if (localCounter_ == 1) {
            jumpTravel_ = 0;
            D3DVECTOR back(startPos_.x - 80.0f, startPos_.y, startPos_.z), front(startPos_.x + 80.0f, startPos_.y, startPos_.z);
            jumpStart_ = moveState_ == JUMP1 ? back : front;
            jumpEnd_ = moveState_ == JUMP1 ? front : back;
        }
        jumpTravel_ += 0.008f;
        if (jumpTravel_ >= 1.0f) jumpTravel_ = 1.0f;
        const D3DVECTOR prev = pos_;
        pos_ = D3DVectorInterpolate(jumpStart_, jumpEnd_, jumpTravel_);
        pos_.y += std::sin(kPi * jumpTravel_) * 92.0f;
        yaw_ = MakeAngleCloseTo(yaw_, FindDirectionXZ(prev, pos_), 0.1f, 0.0f, FLT_MAX);
        pitch_ = MakeAngleCloseTo(pitch_, -FindPitch(prev, pos_), 0.1f, 0.0f, FLT_MAX);
        if (jumpTravel_ > 0.3f && 0.7f > jumpTravel_) {
            frame_ = 4;  // mid-air
        } else {
            walkCounter_++;
            frame_ = (int)((walkCounter_ / 12) % 4);
        }
        // a wave where it crosses the surface
        if (game_->useWater && (prev.y < -38.0f) != (pos_.y < -38.0f))
            game_->particles->CreateWaterWave(D3DVECTOR(pos_.x, -38.0f, pos_.z), false, true);
        if (jumpTravel_ >= 1.0f) {
            clockwise_ = Random(2) == 0;
            jumpReadyTimer_ = typeA_ ? (int)RandomI(2, 5, true) : (int)RandomI(1, 2, true);
            localCounter_ = 0;
            moveState_ = moveState_ == JUMP1 ? SWIM2 : SWIM1;
        }
    }

    MoveState moveState_ = NON;
    bool typeA_, clockwise_ = true;
    float posAngle_ = 0, jumpTravel_ = 0;
    unsigned walkCounter_ = 0;
    int jumpReadyTimer_ = 0;
    D3DVECTOR jumpStart_{0, 0, 0}, jumpEnd_{0, 0, 0};
};
}  // namespace

// ---------------------------------------------------------------------------------- XGameContext

XHero* XGameContext::GetCloserHero(const D3DVECTOR& p) const {
    float a = FLT_MAX, b = FLT_MAX;
    if (heroA && heroA->GetLiveState() == XHero::NORMAL) a = ApproxDistance(p, heroA->GetPos());
    if (heroB && heroB->GetLiveState() == XHero::NORMAL) b = ApproxDistance(p, heroB->GetPos());
    return a > b ? heroB : heroA;
}

float XGameContext::DistanceToCloserHero(const D3DVECTOR& p) const {
    float a = FLT_MAX, b = FLT_MAX;
    if (heroA && heroA->GetLiveState() == XHero::NORMAL) a = ApproxDistance(p, heroA->GetPos());
    if (heroB && heroB->GetLiveState() == XHero::NORMAL) b = ApproxDistance(p, heroB->GetPos());
    return a > b ? b : a;
}

float XGameContext::DistanceToCloserHeroXZ(const D3DVECTOR& p) const {
    float a = FLT_MAX, b = FLT_MAX;
    if (heroA && heroA->GetLiveState() == XHero::NORMAL) a = ApproxDistanceXZ(p, heroA->GetPos());
    if (heroB && heroB->GetLiveState() == XHero::NORMAL) b = ApproxDistanceXZ(p, heroB->GetPos());
    return a > b ? b : a;
}

float XGameContext::DirToCloserHero(const D3DVECTOR& p) const {
    XHero* h = GetCloserHero(p);
    return h ? FindDirectionXZ(p, h->GetPos()) : 0.0f;
}

namespace {
bool FloorEdge(const XGameContext& g, int x, int z, int level) {
    // XMap::IsFloorLevelDifferent (outside counts) or IsInvisibleBlock ('!')
    const MapCollision& m = g.level->Collision();
    if (x < 0 || x >= LevelGrid::kWidth || z < 0 || z >= m.Depth()) return true;
    return m.GetFloorLevel(x, z) != level || g.level->ObjectCode(x, z) == '!';
}
}  // namespace

bool XGameContext::CollisionCheckX_Floor(float& newX, const XCollideBox& box, int level) const {
    int x0 = MapCollision::CellX(newX - box.WidthHalfX(), true), x1 = MapCollision::CellX(newX + box.WidthHalfX(), false);
    int za = MapCollision::CellZ(box.Node1().z, true), zb = MapCollision::CellZ(box.Pos().z, true),
        zc = MapCollision::CellZ(box.Node2().z, false);
    if (box.Pos().x > newX) {
        if (FloorEdge(*this, x0, za, level) || FloorEdge(*this, x0, zb, level) || FloorEdge(*this, x0, zc, level)) {
            newX = (x0 + 1) * 40.0f + box.WidthHalfX();
            return true;
        }
    } else if (newX > box.Pos().x) {
        if (FloorEdge(*this, x1, za, level) || FloorEdge(*this, x1, zb, level) || FloorEdge(*this, x1, zc, level)) {
            newX = x1 * 40.0f - box.WidthHalfX();
            return true;
        }
    }
    return false;
}

bool XGameContext::CollisionCheckZ_Floor(float& newZ, const XCollideBox& box, int level) const {
    int z0 = MapCollision::CellZ(newZ - box.WidthHalfZ(), true), z1 = MapCollision::CellZ(newZ + box.WidthHalfZ(), false);
    int xa = MapCollision::CellX(box.Node1().x, true), xb = MapCollision::CellX(box.Pos().x, true),
        xc = MapCollision::CellX(box.Node2().x, false);
    if (box.Pos().z > newZ) {
        if (FloorEdge(*this, xa, z0, level) || FloorEdge(*this, xb, z0, level) || FloorEdge(*this, xc, z0, level)) {
            newZ = (z0 + 1) * 40.0f + box.WidthHalfZ();
            return true;
        }
    } else if (newZ > box.Pos().z) {
        if (FloorEdge(*this, xa, z1, level) || FloorEdge(*this, xb, z1, level) || FloorEdge(*this, xc, z1, level)) {
            newZ = z1 * 40.0f - box.WidthHalfZ();
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------------- XEnemyBase

XEnemyBase::XEnemyBase(const XGameContext* game, int oid, const D3DVECTOR& pos, SizeType size)
    : game_(game), oid_(oid), sizeType_(size), startPos_(pos), pos_(pos) {}
XEnemyBase::~XEnemyBase() = default;

void XEnemyBase::SetFloorLevel(int level) {
    floorLevel_ = level;
    if (shadow_) shadow_->SetFloorLevel(level);
}

void XEnemyBase::SetPosX(float x) {
    pos_.x = x;
    collideBox_->SetPosX(x);
}
void XEnemyBase::SetPosY(float y) {
    pos_.y = y;
    collideBox_->SetPosY(y);
}
void XEnemyBase::SetPosZ(float z) {
    pos_.z = z;
    collideBox_->SetPosZ(z);
}

bool XEnemyBase::UpdatePosX(float x) {
    bool a = game_->enemies->CollisionCheckX(x, *this);
    bool b = game_->statics->CollisionCheckX(x, *collideBox_, true);
    bool c = game_->platforms->CollisionCheckX(x, *collideBox_, true);
    bool d = game_->CollisionCheckX_Floor(x, *collideBox_, floorLevel_);
    SetPosX(x);
    return a || b || c || d;
}

bool XEnemyBase::UpdatePosZ(float z) {
    bool a = game_->enemies->CollisionCheckZ(z, *this);
    bool b = game_->statics->CollisionCheckZ(z, *collideBox_, true);
    bool c = game_->platforms->CollisionCheckZ(z, *collideBox_, true);
    bool d = game_->CollisionCheckZ_Floor(z, *collideBox_, floorLevel_);
    SetPosZ(z);
    return a || b || c || d;
}

void XEnemyBase::UpdateWorldMatrix() {
    model_->SetRoll(roll_);
    model_->SetPitch(pitch_);
    model_->SetYaw(yaw_);
    model_->SetPos(pos_);
    model_->UpdateWorldMatrix();
}

D3DVECTOR XEnemyBase::GenerateBehindPos() const {
    // 40 ahead along a direction 120..240 degrees off the current one
    float a = NormaliseAngle(dir_ + RandomF(120.0f, 240.0f) * 0.0174533f);
    return D3DVECTOR(pos_.x + std::sin(a) * 40.0f, pos_.y, pos_.z + std::cos(a) * 40.0f);
}

D3DVECTOR XEnemyBase::GenerateRandomPos() const {
    D3DVECTOR p;
    p.x = RandomF(20.0f, 180.0f);
    p.z = RandomF(startPos_.z - 140.0f, startPos_.z + 140.0f);
    p.y = startPos_.y;
    return p;
}

void XEnemyBase::ProcessFrame() {
    if (liveState_ == NON) return;
    localCounter_++;
    if (damageCounter_ > 0 && --damageCounter_ <= 0) model_->UseWhiteMaterial(false);
    if (liveState_ != DEAD) return;
    // dying: a hop while spinning, then the burst
    if (localCounter_ == 1) {
        game_->playSound(kSoundEnemyDie, pos_.z);
        verticalSpeed_ = 4.0f;
    }
    SetPosY(pos_.y + verticalSpeed_);
    yaw_ = NormaliseAngle(yaw_ + 0.244346f);
    verticalSpeed_ -= 0.08f;
    if (verticalSpeed_ < -3.0f) {
        pos_.y += collideBox_->Height() * 0.5f;
        game_->particles->CreateEnemyExplode(pos_, sizeType_ != SMALL);
        if (game_->createEnemyCoin) game_->createEnemyCoin(*this);
        game_->playSound(kSoundEnemyBurst, pos_.z);
        liveState_ = NON;
        return;
    }
    UpdateWorldMatrix();
}

void XEnemyBase::Render(Device* d) {
    if (liveState_ == NON || !game_->IsVisible(pos_)) return;
    if (shadow_) {
        shadow_->SetPosXZ(pos_ + model_->BlendOffset());  // (native: where the model is drawn)
        shadow_->Render(d);
    }
    model_->SetCurrentFrame(frame_);
    model_->Render(d);
}

bool XEnemyBase::CollisionCheck_Fire(const D3DVECTOR& pos, float radius, const D3DVECTOR& heroPos) {
    if (liveState_ != NORMAL) return false;
    if (std::fabs(pos_.z - pos.z) > collideBox_->WidthZ() + radius) return false;
    if (std::fabs(pos_.x - pos.x) > collideBox_->WidthZ() + radius) return false;  // (Z width, as the original)
    if (!model_->CheckCollision_Sphere(pos, radius)) return false;
    Damage(heroPos);
    return true;
}

void XEnemyBase::Damage(const D3DVECTOR& heroPos) {
    if (!damagable_) return;
    game_->playSound(kSoundEnemyHurt, heroPos.z);
    damageCounter_ = 12;
    model_->UseWhiteMaterial(true);
    if (GetConfig().oneHitKills) health_ = 1;  // native: the One-hit kills cheat
    if (--health_ <= 0) {
        localCounter_ = 0;
        liveState_ = DEAD;
    }
}

bool XEnemyBase::CollisionCheck_Hero(XHero* hero) {
    if (liveState_ != NORMAL || !hero || hero->GetLiveState() != XHero::NORMAL) return false;
    if (noCollisionWhenHeroDamaged_ && hero->BeingDamage()) return false;
    if (!collideBox_->CollisionCheck(hero->GetCollideBox())) return false;
    if (!model_->CheckCollision(hero->BodyModel())) return false;
    hero->Damage(*game_->heroWorld);
    hero->KnockBack(pos_);
    return true;
}

// ---------------------------------------------------------------------------------- XEnemyList

XEnemyList::~XEnemyList() = default;

void XEnemyList::Empty() {
    for (auto& s : slots_) s.reset();
    lastOID_ = 0;
}

int XEnemyList::GetNullIndex() const {
    for (int i = 0; i < kSlots; i++)
        if (!slots_[i]) return i;
    return -1;
}

XEnemyBase* XEnemyList::CreateEnemy(const D3DVECTOR& pos, char code) {
    int i = GetNullIndex();
    if (i < 0) return nullptr;
    std::unique_ptr<XEnemyBase> e;
    switch (code) {
        case 'a': e = std::make_unique<Slime>(game_, lastOID_, pos, XEnemyBase::SMALL); break;
        case '1': e = std::make_unique<Slime>(game_, lastOID_, pos, XEnemyBase::KING); break;
        case '5': e = std::make_unique<Slime>(game_, lastOID_, pos, XEnemyBase::QUEEN); break;
        case 'b': e = std::make_unique<Bee>(game_, lastOID_, pos, XEnemyBase::SMALL); break;
        case '3': e = std::make_unique<Bee>(game_, lastOID_, pos, XEnemyBase::KING); break;
        case 'd': e = std::make_unique<Wallaby>(game_, lastOID_, pos, XEnemyBase::SMALL); break;
        case '0': e = std::make_unique<Wallaby>(game_, lastOID_, pos, XEnemyBase::KING); break;
        case 'h': e = std::make_unique<Mushroom>(game_, lastOID_, pos, XEnemyBase::SMALL); break;
        case '2': e = std::make_unique<Mushroom>(game_, lastOID_, pos, XEnemyBase::KING); break;
        case 'c': e = std::make_unique<Squid>(game_, lastOID_, pos, XEnemyBase::SMALL); break;
        case '4': e = std::make_unique<Squid>(game_, lastOID_, pos, XEnemyBase::KING); break;
        case 'x': e = std::make_unique<NeedleBall>(game_, lastOID_, pos, NeedleBall::X_AXIS); break;
        case 'y': e = std::make_unique<NeedleBall>(game_, lastOID_, pos, NeedleBall::Z_AXIS); break;
        case 'j': e = std::make_unique<BarrelEnemy>(game_, lastOID_, pos); break;
        case 'm': e = std::make_unique<NeedleFan>(game_, lastOID_, pos); break;
        case 'e': e = std::make_unique<Turtle>(game_, lastOID_, pos); break;
        case 'f': e = std::make_unique<Kiwi>(game_, lastOID_, pos); break;
        case 'k': e = std::make_unique<Hammer>(game_, lastOID_, pos); break;
        case 'q': e = std::make_unique<Fan>(game_, lastOID_, pos, 4); break;
        case 'r': e = std::make_unique<Fan>(game_, lastOID_, pos, 0); break;
        case 's': e = std::make_unique<Fan>(game_, lastOID_, pos, 2); break;
        case 't': e = std::make_unique<FloorFan>(game_, lastOID_, pos); break;
        case 'g': e = std::make_unique<Ghost>(game_, lastOID_, pos); break;
        case 'v': e = std::make_unique<FenceDoor>(game_, lastOID_, pos, false); break;
        case 'w': e = std::make_unique<FenceDoor>(game_, lastOID_, pos, true); break;
        case 'o': e = std::make_unique<Needle>(game_, lastOID_, pos, false); break;
        case 'p': e = std::make_unique<Needle>(game_, lastOID_, pos, true); break;
        case 'z': e = std::make_unique<JumpNeedle>(game_, lastOID_, pos); break;
        case 'u': e = std::make_unique<Stalactite>(game_, lastOID_, pos); break;
        case 'i': e = std::make_unique<Fish>(game_, lastOID_, pos, game_->world < 2); break;
        default: break;  // the other enemies: stage 5
    }
    slots_[i] = std::move(e);
    lastOID_++;
    return slots_[i].get();
}

void XEnemyList::ProcessFrame() {
    for (auto& s : slots_) {
        if (!s) break;
        s->ProcessFrame();
    }
}

void XEnemyList::Render(Device* d) {
    for (auto& s : slots_) {
        if (!s) break;
        s->Render(d);
    }
}

bool XEnemyList::CollisionCheck_Fire(const D3DVECTOR& pos, float radius, const D3DVECTOR& heroPos) {
    for (auto& s : slots_) {
        if (!s) break;
        if (s->CollisionCheck_Fire(pos, radius, heroPos)) return true;
    }
    return false;
}

XEnemyBase* XEnemyList::GetClosestEnemy(const D3DVECTOR& pos, float rangeZ) const {
    XEnemyBase* closest = nullptr;
    float closestDist = kFloatMax;
    for (auto& s : slots_) {
        if (!s) break;
        if (!s->Chasable() || s->GetLiveState() != XEnemyBase::NORMAL) continue;
        if (std::fabs(s->GetPos().z - pos.z) > rangeZ) continue;
        float d = ApproxDistance(pos, s->GetPos());
        if (closestDist > d) {
            closestDist = d;
            closest = s.get();
        }
    }
    return closest;
}

bool XEnemyList::CollisionCheckX(float& newX, const XEnemyBase& m) const {
    bool hit = false;
    for (auto& s : slots_) {
        if (!s) break;
        if (s->GetLiveState() != XEnemyBase::NORMAL || !s->Collidable() || s->GetOID() == m.GetOID()) continue;
        if (s->GetCollideBox().CollisionCheckX(newX, m.GetCollideBox(), true)) hit = true;
    }
    return hit;
}

bool XEnemyList::CollisionCheckZ(float& newZ, const XEnemyBase& m) const {
    bool hit = false;
    for (auto& s : slots_) {
        if (!s) break;
        if (s->GetLiveState() != XEnemyBase::NORMAL || !s->Collidable() || s->GetOID() == m.GetOID()) continue;
        if (s->GetCollideBox().CollisionCheckZ(newZ, m.GetCollideBox(), true)) hit = true;
    }
    return hit;
}
