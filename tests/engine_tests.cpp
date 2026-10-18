// Unit tests for the pure helpers in the mod. No framework: the mod must stay
// a single file built by Windhawk's bundled clang, so tests stay just as light.

#include <cstdio>

#include "../taskbar-cat.wh.cpp"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                   \
    do {                                                              \
        g_checks++;                                                   \
        if (!(cond)) {                                                \
            g_failures++;                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                             \
    } while (0)

constexpr int kPixels = cat::kSpriteSize * cat::kSpriteSize;

static void TestComputeScale() {
    CHECK(cat::ComputeScale(96, 0) == 1);    // 100%
    CHECK(cat::ComputeScale(120, 0) == 1);   // 125% rounds down
    CHECK(cat::ComputeScale(144, 0) == 2);   // 150% rounds up
    CHECK(cat::ComputeScale(192, 0) == 2);   // 200%
    CHECK(cat::ComputeScale(480, 0) == cat::kMaxScale);  // clamped
    CHECK(cat::ComputeScale(10, 0) == 1);    // never zero
    CHECK(cat::ComputeScale(96, 3) == 3);    // fixed size wins
    CHECK(cat::ComputeScale(96, 9) == 1);    // out of range means auto
}

static void TestPremultiply() {
    CHECK(cat::PremultiplyArgb(0x80FF0000) == 0x80800000);
    CHECK(cat::PremultiplyArgb(0x00FFFFFF) == 0x00000000);
    CHECK(cat::PremultiplyArgb(0xFF123456) == 0xFF123456);
}

static void TestDecodeRleFrame() {
    const uint32_t palette[] = {0x00000000, 0xFF112233};
    const uint8_t data[] = {0x21, 0x00};  // three of index 1, one of index 0
    uint32_t out[4] = {};
    CHECK(cat::DecodeRleFrame(data, 2, palette, 2, out, 4));
    CHECK(out[0] == 0xFF112233 && out[2] == 0xFF112233 && out[3] == 0);

    CHECK(!cat::DecodeRleFrame(data, 2, palette, 2, out, 5));  // too short
    CHECK(!cat::DecodeRleFrame(data, 2, palette, 2, out, 3));  // overflow
    const uint8_t badIndex[] = {0x37};
    CHECK(!cat::DecodeRleFrame(badIndex, 1, palette, 2, out, 4));
}

// Frames that leave the floor on purpose: mid-pounce, dangling, hearts.
static bool IsAirborneFrame(int frame) {
    for (int anim : {sprites::kPounce, sprites::kDangle, sprites::kHeart, sprites::kAlert,
                     sprites::kZzz, sprites::kButterfly, sprites::kNote, sprites::kFish,
                     sprites::kHatWitch, sprites::kHatSanta, sprites::kHatParty}) {
        const cat::AnimInfo& info = sprites::kAnims[anim];
        if (frame >= info.firstFrame && frame < info.firstFrame + info.frameCount) {
            return true;
        }
    }
    return false;
}

static void TestGeneratedSprites() {
    static uint32_t frame[kPixels];
    for (int i = 0; i < sprites::kFrameCount; i++) {
        uint16_t begin = sprites::kFrameOffsets[i];
        uint16_t end = sprites::kFrameOffsets[i + 1];
        CHECK(cat::DecodeRleFrame(sprites::kFrameData + begin, end - begin,
                                  sprites::kPalette, std::size(sprites::kPalette),
                                  frame, kPixels));
        // Every pose must touch the bottom row so the cat sits on the edge.
        bool grounded = false;
        for (int x = 0; x < cat::kSpriteSize; x++) {
            grounded |= (frame[kPixels - cat::kSpriteSize + x] >> 24) != 0;
        }
        CHECK(grounded || IsAirborneFrame(i));
    }
    int next = 0;
    for (const cat::AnimInfo& anim : sprites::kAnims) {
        CHECK(anim.firstFrame == next);
        CHECK(anim.frameCount > 0 && anim.frameMs > 0);
        next += anim.frameCount;
    }
    CHECK(next == sprites::kFrameCount);
}

static void TestFrameInAnim() {
    cat::AnimInfo loop = {0, 4, 100, true};
    CHECK(cat::FrameInAnim(loop, 0) == 0);
    CHECK(cat::FrameInAnim(loop, 250) == 2);
    CHECK(cat::FrameInAnim(loop, 450) == 0);  // wraps
    CHECK(!cat::AnimDone(loop, 10000));

    cat::AnimInfo once = {0, 3, 100, false};
    CHECK(cat::FrameInAnim(once, 1000) == 2);  // holds last frame
    CHECK(!cat::AnimDone(once, 299));
    CHECK(cat::AnimDone(once, 300));
}

static void TestWindowOrigin() {
    RECT bottomBar = {0, 1032, 1920, 1080};
    POINT p = cat::ComputeWindowOrigin(bottomBar, 100.7, 32);
    CHECK(p.x == 100);
    CHECK(p.y == 1048);

    RECT topBar = {0, 0, 1920, 48};
    p = cat::ComputeWindowOrigin(topBar, 0, 32);
    CHECK(p.y == 16);
}

static void TestTaskbarGeometry() {
    RECT monitor = {0, 0, 1920, 1080};
    RECT shown = {0, 1032, 1920, 1080};
    RECT autoHidden = {0, 1078, 1920, 1126};
    RECT vertical = {0, 0, 48, 1080};
    CHECK(cat::IsHorizontalTaskbar(shown));
    CHECK(!cat::IsHorizontalTaskbar(vertical));
    CHECK(!cat::IsMostlyOffMonitor(shown, monitor));
    CHECK(cat::IsMostlyOffMonitor(autoHidden, monitor));
}

using engine::Activity;
using engine::CatState;
using engine::Needs;

constexpr int kDayHour = 14;
constexpr int kNightHour = 2;

static engine::World MakeWorld(int hour = kDayHour, uint32_t idle = 0,
                               bool stayStill = false) {
    engine::World w = {};
    w.minX = 0;
    w.maxX = 1000;
    w.walkSpeed = 30;
    w.localHour = hour;
    w.userIdleSeconds = idle;
    w.stayStill = stayStill;
    w.scale = 1;
    w.idleNapSeconds = engine::kIdleNapSeconds;
    w.nightAware = true;
    w.chaseCursor = true;
    w.activityLevel = engine::ActivityLevel::Normal;
    return w;
}

static CatState CatWith(Needs needs, uint32_t seed = 1, double x = 500) {
    return engine::Initial(seed, x, 0, needs);
}

// Runs the engine for up to maxMs in 33 ms ticks, stopping early when stop()
// says so. Returns the final state.
template <typename Stop>
static CatState Run(CatState s, const engine::World& w, uint64_t startMs,
                    uint64_t maxMs, Stop stop, uint64_t* endMs = nullptr) {
    uint64_t now = startMs;
    while (now < startMs + maxMs && !stop(s)) {
        now += 33;
        s = engine::Step(s, now, 0.033, w);
    }
    if (endMs) {
        *endMs = now;
    }
    return s;
}

static void TestStartsSittingWithTailFlick() {
    CatState s = CatWith(engine::kStartingNeeds);
    CHECK(s.activity == Activity::Sit);
    CHECK(s.anim == sprites::kSitTail);
    CHECK(s.untilMs >= engine::kSitMinMs && s.untilMs <= engine::kSitMaxMs);

    const cat::AnimInfo& flick = sprites::kAnims[sprites::kSitTail];
    uint64_t afterFlick = (uint64_t)flick.frameMs * flick.frameCount;
    CatState next = engine::Step(s, afterFlick, 0.03, MakeWorld());
    CHECK(next.activity == Activity::Sit && next.anim == sprites::kSit);
    CHECK(next.untilMs == s.untilMs);
}

static void TestNeedsDrift() {
    Needs n = {50, 50, 50};
    Needs walked = engine::DriftNeeds(n, Activity::Walk, 10, false);
    CHECK(walked.energy < 50 && walked.boredom < 50);
    Needs sat = engine::DriftNeeds(n, Activity::Sit, 10, false);
    CHECK(sat.boredom > 50);
    Needs slept = engine::DriftNeeds(n, Activity::Sleep, 10, false);
    CHECK(slept.energy > 50);
    CHECK(n.energy == 50);  // input untouched

    Needs nightWalk = engine::DriftNeeds(n, Activity::Walk, 10, true);
    CHECK(fabs((50 - nightWalk.energy) - 2 * (50 - walked.energy)) < 1e-9);

    Needs clamped = engine::DriftNeeds({99, 1, 100}, Activity::Sleep, 100, false);
    CHECK(clamped.energy == engine::kNeedMax);
    clamped = engine::DriftNeeds({1, 1, 1}, Activity::Trot, 100, false);
    CHECK(clamped.energy == 0 && clamped.boredom == 0);
}

static void TestNightIsHoursElevenToSeven() {
    CHECK(engine::IsNight(23) && engine::IsNight(0) && engine::IsNight(6));
    CHECK(!engine::IsNight(7) && !engine::IsNight(14) && !engine::IsNight(22));
}

static void TestExhaustedCatAlwaysNaps() {
    for (uint32_t seed = 1; seed <= 50; seed++) {
        CatState s = CatWith({10, 90, 50}, seed);
        s = engine::Step(s, s.untilMs, 0.03, MakeWorld());
        CHECK(s.activity == Activity::LieDown);
    }
}

// Kneading before a nap counts as choosing to nap.
static bool IsNapping(const CatState& s) {
    return s.activity == Activity::LieDown ||
           (s.activity == Activity::Knead && s.napAfterKnead);
}

static int CountNextActivity(Needs needs, Activity wanted, int hour = kDayHour) {
    int count = 0;
    for (uint32_t seed = 1; seed <= 400; seed++) {
        CatState s = CatWith(needs, seed);
        s = engine::Step(s, s.untilMs, 0.0, MakeWorld(hour));
        count += wanted == Activity::LieDown ? IsNapping(s) : s.activity == wanted;
    }
    return count;
}

static void TestBoredCatMovesMore() {
    int boredWalks = CountNextActivity({80, 90, 50}, Activity::Walk) +
                     CountNextActivity({80, 90, 50}, Activity::Trot);
    int calmWalks = CountNextActivity({80, 5, 50}, Activity::Walk) +
                    CountNextActivity({80, 5, 50}, Activity::Trot);
    CHECK(boredWalks > calmWalks * 2);
    CHECK(CountNextActivity({80, 5, 50}, Activity::Trot) == 0);  // no zoomies when calm
    CHECK(CountNextActivity({80, 95, 50}, Activity::Trot) > 0);
}

static void TestTiredCatNapsMoreAtNight() {
    int day = CountNextActivity({25, 20, 50}, Activity::LieDown, kDayHour);
    int night = CountNextActivity({25, 20, 50}, Activity::LieDown, kNightHour);
    CHECK(day > 0);
    CHECK(night > day);
    CHECK(CountNextActivity({80, 20, 50}, Activity::LieDown) == 0);  // rested cats don't nap
}

static void TestWalkStaysInBoundsAndArrives() {
    engine::World w = MakeWorld();
    for (uint32_t seed = 1; seed <= 100; seed++) {
        CatState s = CatWith({80, 50, 50}, seed, seed % 2 ? 5.0 : 995.0);
        s = engine::StartWalk(s, w, 0, seed % 3 == 0);
        CHECK(s.targetX >= w.minX && s.targetX <= w.maxX);
        // Near an edge it heads back into the room.
        CHECK(fabs(s.targetX - s.x) >= w.walkSpeed * engine::kMinStrollSeconds - 1e-6);
        CHECK(s.dir == (s.targetX >= s.x ? 1 : -1));

        double target = s.targetX;
        s = Run(s, w, 0, 60000, [](const CatState& c) {
            return c.activity != Activity::Walk && c.activity != Activity::Trot;
        });
        CHECK(s.activity != Activity::Walk || s.targetX != target);
        CHECK(s.x >= w.minX && s.x <= w.maxX);
    }
}

static void TestTrotIsFasterThanWalk() {
    engine::World w = MakeWorld();
    CatState walk = engine::StartWalk(CatWith({80, 50, 50}, 9, 100), w, 0, false);
    CatState trot = engine::StartWalk(CatWith({80, 50, 50}, 9, 100), w, 0, true);
    CatState walked = engine::Step(walk, 33, 1.0, w);
    CatState trotted = engine::Step(trot, 33, 1.0, w);
    CHECK(fabs(trotted.x - trot.x) > fabs(walked.x - walk.x) * 2);
    CHECK(trot.playbackPercent > 100);
}

static void TestNapRestoresEnergyThenWakes() {
    engine::World w = MakeWorld();
    CatState s = engine::StartLieDown(CatWith({30, 20, 50}), 0, false);
    const cat::AnimInfo& wake = sprites::kAnims[sprites::kWake];
    // Lying down plays the wake-up frames backwards.
    CHECK(engine::CurrentFrame(s, 0) == wake.firstFrame + wake.frameCount - 1);

    uint64_t now = 0;
    s = Run(s, w, 0, 10000, [](const CatState& c) { return c.activity == Activity::Sleep; }, &now);
    CHECK(s.activity == Activity::Sleep);
    s = Run(s, w, now, engine::kSleepMaxMs + 1000,
            [](const CatState& c) { return c.activity != Activity::Sleep; }, &now);
    CHECK(s.activity == Activity::WakeUp);
    CHECK(s.needs.energy > 70);
    CHECK(engine::CurrentFrame(s, s.startMs) == wake.firstFrame);
    s = Run(s, w, now, 5000, [](const CatState& c) { return c.activity == Activity::Sit; });
    CHECK(s.activity == Activity::Sit);
}

static void TestSleepsWhileUserAwayAndGreetsOnReturn() {
    engine::World away = MakeWorld(kDayHour, engine::kIdleNapSeconds);
    CatState s = engine::StartWalk(CatWith({90, 50, 50}), away, 0, false);
    s = engine::Step(s, 33, 0.033, away);
    CHECK(s.activity == Activity::LieDown && s.napForIdle);

    uint64_t now = 0;
    s = Run(s, away, 33, engine::kSleepMaxMs * 2,
            [](const CatState& c) { return c.activity == Activity::WakeUp; }, &now);
    CHECK(s.activity == Activity::Sleep);  // full energy, still asleep: you're away

    s = engine::Step(s, now + 33, 0.033, MakeWorld());
    CHECK(s.activity == Activity::WakeUp && !s.napForIdle);
}

static void TestStayStillNeverMoves() {
    engine::World w = MakeWorld(kDayHour, 0, true);
    CatState s = CatWith({80, 100, 50}, 3, 50);
    s = Run(s, w, 0, 600000, [](const CatState& c) {
        return c.activity == Activity::Walk || c.activity == Activity::Trot;
    });
    CHECK(s.activity != Activity::Walk && s.activity != Activity::Trot);
    CHECK(s.x == 50);
}

static void TestClampsToShrunkenTaskbar() {
    engine::World w = MakeWorld();
    w.maxX = 400;
    CatState s = engine::Step(CatWith({80, 50, 50}, 3, 900), 10, 0.03, w);
    CHECK(s.x == 400);
}

static void TestPetting() {
    CatState sleeping = engine::StartSleep(CatWith({30, 20, 80}), 0, false);
    CatState petted = engine::OnClick(sleeping, 10);
    CHECK(petted.activity == Activity::WakeUp);
    CHECK(petted.needs.affection == 80 - engine::kPetAffection);

    CatState walking = engine::StartWalk(CatWith({80, 50, 10}), MakeWorld(), 0, false);
    CatState meowed = engine::OnClick(walking, 10);
    CHECK(meowed.activity == Activity::Meow);
    CHECK(meowed.needs.affection == 0);
    meowed = Run(meowed, MakeWorld(), 10, 2000,
                 [](const CatState& c) { return c.activity != Activity::Meow; });
    CHECK(meowed.activity == Activity::Sit);
}

static void TestLongDayStaysSane() {
    // Eight simulated hours: the cat should do a bit of everything and its
    // needs must stay in range.
    engine::World w = MakeWorld();
    CatState s = CatWith(engine::kStartingNeeds, 1234);
    bool did[(int)Activity::Dance + 1] = {};  // Dance is the last activity
    uint64_t now = 0;
    for (int i = 0; i < 8 * 3600 * 10; i++) {
        now += 100;
        s = engine::Step(s, now, 0.1, w);
        did[(int)s.activity] = true;
        CHECK(s.needs.energy >= 0 && s.needs.energy <= engine::kNeedMax);
        if (g_failures) {
            return;
        }
    }
    // With no mouse around, every self-driven activity should show up.
    for (Activity a : {Activity::Walk, Activity::Trot, Activity::Sit, Activity::Loaf,
                       Activity::LieDown, Activity::Sleep, Activity::WakeUp}) {
        CHECK(did[(int)a]);
    }
}

static engine::World CursorWorld(double cursorX, double speed, bool hovered = false) {
    engine::World w = MakeWorld();
    w.cursorOnTaskbar = true;
    w.cursorX = cursorX;
    w.cursorSpeed = speed;
    w.hovered = hovered;
    return w;
}

template <typename Done>
static CatState RunUntil(CatState s, const engine::World& w, uint64_t* now,
                         uint64_t maxMs, Done done) {
    return Run(s, w, *now, maxMs, done, now);
}

static void TestComposeCanvas() {
    static uint32_t catFrame[kPixels];
    static uint32_t effect[kPixels];
    static uint32_t canvas[cat::kCanvasWidth * cat::kCanvasHeight];
    catFrame[0] = 0xFF0000FF;  // top-left of the cat
    effect[0] = 0xFFFF0000;    // opaque effect pixel
    effect[1] = 0x80000000;    // half-transparent black
    int catRow = cat::kEffectHeadroom * cat::kCanvasWidth;

    cat::ComposeCanvas(catFrame, false, nullptr, 0, 0, nullptr, 0, 0, canvas);
    CHECK(canvas[catRow] == 0xFF0000FF);
    CHECK(canvas[0] == 0);  // headroom stays empty

    cat::ComposeCanvas(catFrame, true, nullptr, 0, 0, nullptr, 0, 0, canvas);
    CHECK(canvas[catRow + cat::kSpriteSize - 1] == 0xFF0000FF);  // mirrored

    cat::ComposeCanvas(catFrame, false, nullptr, 0, 0, effect, 0, cat::kEffectHeadroom, canvas);
    CHECK(canvas[catRow] == 0xFFFF0000);      // effect drawn over the cat
    CHECK(canvas[catRow + 1] == 0x80000000);  // over transparent
    cat::ComposeCanvas(catFrame, false, nullptr, 0, 0, effect, -1, cat::kEffectHeadroom, canvas);
    CHECK(canvas[catRow] == cat::BlendOver(0x80000000, 0xFF0000FF));
    cat::ComposeCanvas(catFrame, false, nullptr, 0, 0, effect, 500, 500, canvas);  // clipped
    CHECK(canvas[catRow] == 0xFF0000FF);
}

static void TestHoverPurrsWithHearts() {
    CatState s = CatWith({80, 30, 60});
    s = engine::Step(s, 100, 0.03, CursorWorld(516, 0, true));
    CHECK(s.activity == Activity::Purr && s.effect == engine::Effect::Hearts);
    engine::EffectDraw heart = engine::CurrentEffect(s, 200);
    CHECK(heart.frame >= sprites::kAnims[sprites::kHeart].firstFrame);
    CHECK(engine::CurrentEffect(CatWith({80, 30, 60}), 0).frame == -1);

    // Keeps purring while hovered, then a little longer.
    CatState still = engine::Step(s, 100 + engine::kPurrMinMs * 4, 1.0,
                                  CursorWorld(516, 0, true));
    CHECK(still.activity == Activity::Purr);
    CHECK(still.needs.affection < s.needs.affection);
    CatState left = engine::Step(s, 100 + engine::kPurrMinMs / 2, 0.03, MakeWorld());
    CHECK(left.activity == Activity::Purr);
    left = engine::Step(left, 100 + engine::kPurrMinMs + 1, 0.03, MakeWorld());
    CHECK(left.activity == Activity::Sit && left.effect == engine::Effect::None);

    CatState sleeping = engine::StartSleep(CatWith({30, 20, 60}), 0, false);
    CHECK(engine::Step(sleeping, 10, 0.03, CursorWorld(516, 0, true)).activity ==
          Activity::Sleep);  // hovering doesn't wake it
}

static void TestLooksAtNearbyCursor() {
    CatState s = CatWith({80, 30, 50}, 1, 500);  // facing right
    s = engine::Step(s, 100, 0.03, CursorWorld(440, 0));
    CHECK(s.dir == -1);
    s = engine::Step(s, 200, 0.03, CursorWorld(600, 0));
    CHECK(s.dir == 1);
    s = engine::Step(s, 300, 0.03, CursorWorld(5, 0));  // too far to notice
    CHECK(s.dir == 1);
}

static void TestFastCursorTriggersHuntAndPounce() {
    engine::World racing = CursorWorld(650, 2000);
    CatState s = CatWith({80, 30, 50}, 1, 500);
    uint64_t now = 0;
    s = RunUntil(s, racing, &now, 10000,
                 [](const CatState& c) { return c.activity == Activity::Crouch; });
    CHECK(s.activity == Activity::Crouch);
    CHECK(s.dir == 1);

    s = RunUntil(s, racing, &now, 5000,
                 [](const CatState& c) { return c.activity == Activity::Pounce; });
    CHECK(s.activity == Activity::Pounce);
    CHECK(fabs(s.targetX - (650 - cat::kSpriteSize / 2.0)) < 1e-9);

    double highest = 0;
    s = RunUntil(s, racing, &now, 5000, [&highest](const CatState& c) {
        highest = c.lift > highest ? c.lift : highest;
        return c.activity != Activity::Pounce;
    });
    CHECK(highest > 5);
    CHECK(s.activity == Activity::Land && s.lift == 0);
    CHECK(fabs(s.x - s.targetX) < 1e-9);

    // Cooldown: no second hunt straight away.
    s = engine::StartSit(s, now);
    CatState again = Run(s, racing, now, engine::kHuntCooldownMs - 2000,
                         [](const CatState& c) { return c.activity == Activity::Crouch; });
    CHECK(again.activity != Activity::Crouch);
}

static void TestNoHuntWhenSlowTiredOrClose() {
    auto crouched = [](const CatState& c) { return c.activity == Activity::Crouch; };
    CHECK(Run(CatWith({80, 30, 50}, 1, 500), CursorWorld(650, 50), 0, 3000, crouched)
              .activity != Activity::Crouch);  // slow cursor
    CHECK(Run(CatWith({80, 30, 50}, 1, 500), CursorWorld(510, 2000), 0, 3000, crouched)
              .activity != Activity::Crouch);  // cursor right on top of it
    CHECK(Run(CatWith({20, 30, 50}, 1, 500), CursorWorld(650, 2000), 0, 3000, crouched)
              .activity != Activity::Crouch);  // too tired
}

static void TestPounceReachIsLimited() {
    CatState s = engine::StartPounce(CatWith({80, 30, 50}, 1, 100), CursorWorld(950, 2000), 0);
    CHECK(s.targetX == 100 + cat::kSpriteSize * engine::kMaxPounceWidths);
}

static void TestDoubleClickBellyRoll() {
    engine::World w = MakeWorld();
    CatState s = engine::OnDoubleClick(CatWith({80, 30, 50}), 0);
    CHECK(s.activity == Activity::RollOver);
    uint64_t now = 0;
    s = RunUntil(s, w, &now, 2000, [](const CatState& c) { return c.activity == Activity::BellyUp; });
    CHECK(s.activity == Activity::BellyUp);
    s = RunUntil(s, w, &now, 10000, [](const CatState& c) { return c.activity == Activity::RollBack; });
    CHECK(s.activity == Activity::RollBack && s.reverse);
    s = RunUntil(s, w, &now, 2000, [](const CatState& c) { return c.activity == Activity::Sit; });
    CHECK(s.activity == Activity::Sit);

    CatState sleeping = engine::StartSleep(CatWith({30, 20, 50}), 0, false);
    CHECK(engine::OnDoubleClick(sleeping, 10).activity == Activity::Sleep);
}

static void TestPickUpCarryDropLand() {
    engine::World w = MakeWorld();
    CatState s = engine::OnPickUp(engine::StartSleep(CatWith({30, 20, 50}), 0, false), 0);
    CHECK(s.activity == Activity::Held && s.anim == sprites::kDangle);
    s = engine::OnHeldMove(s, 1500, 200);  // past the taskbar's end, in the air
    s = engine::Step(s, 50, 0.033, w);
    CHECK(s.x == 1500 && s.lift == 200);  // held cats aren't clamped

    s = engine::OnDrop(s, w, 100);
    CHECK(s.activity == Activity::Fall && s.x == w.maxX);
    uint64_t now = 100;
    s = RunUntil(s, w, &now, 5000, [](const CatState& c) { return c.activity != Activity::Fall; });
    CHECK(s.activity == Activity::Land && s.lift == 0);
    s = RunUntil(s, w, &now, 2000, [](const CatState& c) { return c.activity != Activity::Land; });
    CHECK(s.activity == Activity::Sit);

    CatState onFloor = engine::OnDrop(engine::OnPickUp(CatWith({80, 30, 50}), 0), w, 10);
    CHECK(onFloor.activity == Activity::Land);  // dropped without being lifted
    CHECK(engine::OnHeldMove(CatWith({80, 30, 50}), 9, 9).x == 500);  // not held: ignored
}

static void TestMenuCommands() {
    engine::World w = MakeWorld();
    CatState awake = CatWith({50, 30, 90});
    CatState asleep = engine::StartSleep(CatWith({30, 20, 90}), 0, false);

    CatState petted = engine::OnMenu(awake, engine::MenuCommand::Pet, w, 0);
    CHECK(petted.activity == Activity::Purr && petted.untilMs == engine::kPetPurrMs);
    CHECK(engine::OnMenu(asleep, engine::MenuCommand::Pet, w, 0).activity == Activity::WakeUp);

    CatState fed = engine::OnMenu(awake, engine::MenuCommand::Treat, w, 0);
    CHECK(fed.activity == Activity::Purr);
    CHECK(fed.needs.energy == 50 + engine::kTreatEnergy);
    CHECK(fed.needs.affection == 90 - engine::kTreatAffection);

    CHECK(engine::OnMenu(awake, engine::MenuCommand::Nap, w, 0).activity == Activity::LieDown);
    CHECK(engine::OnMenu(asleep, engine::MenuCommand::Nap, w, 0).activity == Activity::Sleep);
    CHECK(engine::OnMenu(asleep, engine::MenuCommand::Wake, w, 0).activity == Activity::WakeUp);
    CHECK(engine::OnMenu(awake, engine::MenuCommand::Wake, w, 0).activity == Activity::Sit);

    CatState moved = engine::OnMenu(CatWith({80, 30, 50}, 1, 100), engine::MenuCommand::MoveOver, w, 0);
    CHECK(moved.activity == Activity::Walk);
    CHECK(moved.targetX == 100 + cat::kSpriteSize * engine::kMoveOverWidths);
    moved = engine::OnMenu(CatWith({80, 30, 50}, 1, 990), engine::MenuCommand::MoveOver, w, 0);
    CHECK(moved.targetX < 990);  // no room on the right: goes left
}

static void TestClingyCatSeeksCursor() {
    int seeks = 0;
    for (uint32_t seed = 1; seed <= 200; seed++) {
        CatState s = CatWith({80, 5, 95}, seed, 100);
        s = engine::Step(s, s.untilMs, 0.0, CursorWorld(900, 0));
        seeks += s.activity == Activity::Walk && s.targetX == 900 - cat::kSpriteSize / 2.0;
    }
    CHECK(seeks > 20);
}

static void TestWindowOpenedAlertsOrStartles() {
    engine::World w = MakeWorld();
    int alerts = 0, startles = 0;
    for (uint32_t seed = 1; seed <= 200; seed++) {
        CatState s = engine::OnWindowOpened(CatWith({80, 30, 50}, seed), 100);
        CHECK(s.effect == engine::Effect::Alert);
        alerts += s.activity == Activity::Alert;
        startles += s.activity == Activity::Startle;
    }
    CHECK(alerts + startles == 200);
    CHECK(startles > 20 && startles < 100);  // roughly kStartleChancePercent

    CatState alert = CatWith({80, 30, 50});
    for (uint32_t seed = 1; alert.activity != Activity::Alert; seed++) {
        alert = engine::OnWindowOpened(CatWith({80, 30, 50}, seed), 100);
    }
    engine::EffectDraw mark = engine::CurrentEffect(alert, 150);
    CHECK(mark.frame >= sprites::kAnims[sprites::kAlert].firstFrame);
    alert = engine::Step(alert, 100 + engine::kAlertMs, 0.03, w);
    CHECK(alert.activity == Activity::Sit);

    // Cooldown and sleeping cats.
    CHECK(engine::OnWindowOpened(alert, 200).activity == Activity::Sit);
    CatState sleeping = engine::StartSleep(CatWith({30, 20, 50}), 0, false);
    CHECK(engine::OnWindowOpened(sleeping, 100).activity == Activity::Sleep);
}

static void TestStartleHopsAndLands() {
    CatState s = CatWith({80, 30, 50});
    for (uint32_t seed = 1; s.activity != Activity::Startle; seed++) {
        s = engine::OnWindowOpened(CatWith({80, 30, 50}, seed), 0);
    }
    double highest = 0;
    uint64_t now = 0;
    s = RunUntil(s, MakeWorld(), &now, 3000, [&highest](const CatState& c) {
        highest = c.lift > highest ? c.lift : highest;
        return c.activity != Activity::Startle;
    });
    CHECK(highest > 3);
    CHECK(s.activity == Activity::Land && s.lift == 0 && s.x == 500);
}

static void TestFlashingButtonSendsCatOver() {
    engine::World w = MakeWorld();
    CatState s = engine::OnAttentionRequest(CatWith({80, 30, 50}, 1, 100), w, 0, 484);
    CHECK(s.activity == Activity::Trot && s.targetX == 484);
    CHECK(s.effect == engine::Effect::Alert);
    s = Run(s, w, 0, 30000, [](const CatState& c) { return c.activity != Activity::Trot; });
    CHECK(s.x == 484);

    engine::World still = MakeWorld(kDayHour, 0, true);
    CHECK(engine::OnAttentionRequest(CatWith({80, 30, 50}), still, 0, 100).activity ==
          Activity::Sit);
}

static void TestGreetsYouAfterIdleNap() {
    engine::World away = MakeWorld(kDayHour, engine::kIdleNapSeconds);
    CatState s = CatWith({90, 30, 50});
    uint64_t now = 0;
    s = RunUntil(s, away, &now, 10000, [](const CatState& c) { return c.activity == Activity::Sleep; });
    CHECK(s.activity == Activity::Sleep && s.napForIdle);
    s = engine::Step(s, now + 33, 0.033, MakeWorld());
    CHECK(s.activity == Activity::WakeUp && s.greetPending);
    now += 33;
    s = RunUntil(s, MakeWorld(), &now, 5000, [](const CatState& c) { return c.activity == Activity::Meow; });
    CHECK(s.activity == Activity::Meow && !s.greetPending);
}

static void TestUnlockSaysHello() {
    CatState awake = engine::OnUserReturned(CatWith({80, 30, 50}), 0);
    CHECK(awake.activity == Activity::Meow);
    CatState asleep = engine::OnUserReturned(
        engine::StartSleep(CatWith({30, 20, 50}), 0, false), 0);
    CHECK(asleep.activity == Activity::WakeUp && asleep.greetPending);
    CatState held = engine::OnUserReturned(engine::OnPickUp(CatWith({80, 30, 50}), 0), 0);
    CHECK(held.activity == Activity::Held);
}

static void TestRoamRange() {
    using cat::RoamZone;
    cat::RoamRange whole = cat::ComputeRoamRange(1920, 32, RoamZone::Whole, 1607, 300, 50, 50);
    CHECK(whole.minX == 0 && whole.maxX == 1888);

    cat::RoamRange tray = cat::ComputeRoamRange(1920, 32, RoamZone::AvoidTray, 1607, 300, 0, 0);
    CHECK(tray.minX == 0 && tray.maxX == 1575);
    tray = cat::ComputeRoamRange(1920, 32, RoamZone::AvoidTray, 0, 300, 0, 0);  // tray unknown
    CHECK(tray.maxX == 1920 - 300 - 32);

    cat::RoamRange custom = cat::ComputeRoamRange(1920, 32, RoamZone::Custom, 1607, 300, 100, 400);
    CHECK(custom.minX == 100 && custom.maxX == 1920 - 400 - 32);
    custom = cat::ComputeRoamRange(1920, 32, RoamZone::Custom, 0, 300, 1500, 1500);
    CHECK(custom.maxX == custom.minX);  // margins overlap: pinned in place
}

static int CountRestful(engine::ActivityLevel level) {
    int restful = 0;
    for (uint32_t seed = 1; seed <= 400; seed++) {
        engine::World w = MakeWorld();
        w.activityLevel = level;
        CatState s = CatWith({30, 40, 50}, seed);
        s = engine::Step(s, s.untilMs, 0.0, w);
        restful += s.activity == Activity::Loaf || IsNapping(s);
    }
    return restful;
}

static void TestActivityLevel() {
    int lazyCount = CountRestful(engine::ActivityLevel::Lazy);
    int normalCount = CountRestful(engine::ActivityLevel::Normal);
    int hyperCount = CountRestful(engine::ActivityLevel::Hyper);
    CHECK(lazyCount > normalCount && normalCount > hyperCount);  // "hyper" is a Windows macro
}

static void TestSettingsSwitchesInTheWorld() {
    // Idle naps off.
    engine::World neverNap = MakeWorld(kDayHour, 100000);
    neverNap.idleNapSeconds = 0;
    CHECK(engine::Step(CatWith({90, 30, 50}), 10, 0.03, neverNap).activity == Activity::Sit);
    // Custom idle threshold.
    engine::World quickNap = MakeWorld(kDayHour, 61);
    quickNap.idleNapSeconds = 60;
    CHECK(engine::Step(CatWith({90, 30, 50}), 10, 0.03, quickNap).activity == Activity::LieDown);

    // Night ignored when not night-aware.
    engine::World night = MakeWorld(kNightHour);
    night.nightAware = false;
    CHECK(!engine::IsSleepyTime(night));
    CHECK(engine::IsSleepyTime(MakeWorld(kNightHour)));

    // No hunting when chasing is off.
    engine::World racing = CursorWorld(650, 2000);
    racing.chaseCursor = false;
    CatState s = Run(CatWith({80, 30, 50}, 1, 500), racing, 0, 5000,
                     [](const CatState& c) { return c.activity == Activity::Crouch; });
    CHECK(s.activity != Activity::Crouch);
}

static void TestMoodNames() {
    CHECK(wcscmp(engine::MoodName(CatWith({80, 30, 50})), L"content") == 0);
    CHECK(wcscmp(engine::MoodName(CatWith({20, 30, 50})), L"sleepy") == 0);
    CHECK(wcscmp(engine::MoodName(CatWith({80, 90, 50})), L"bored") == 0);
    CHECK(wcscmp(engine::MoodName(CatWith({80, 30, 95})), L"wants attention") == 0);
    CHECK(wcscmp(engine::MoodName(engine::StartSleep(CatWith({30, 30, 50}), 0, false)),
                 L"asleep") == 0);
    CHECK(wcscmp(engine::MoodName(engine::OnPickUp(CatWith({80, 30, 50}), 0)),
                 L"not amused") == 0);
}

static void TestRestoreNeeds() {
    Needs saved = {20, 90, 40};
    Needs soon = engine::RestoreNeeds(saved, 5);
    CHECK(soon.energy == 20 && soon.boredom == 90 && soon.affection == 40);

    Needs later = engine::RestoreNeeds(saved, 120);
    CHECK(later.energy == engine::kRestedEnergy);
    CHECK(later.boredom == 30);
    CHECK(later.affection == 40 + engine::kMissedYouMax);

    Needs junk = engine::RestoreNeeds({-50, 500, 99}, 1000);  // corrupt store
    CHECK(junk.energy == engine::kRestedEnergy && junk.boredom == 30);
    CHECK(junk.affection == engine::kNeedMax);
}

static bool InAnim(int frame, int anim) {
    const cat::AnimInfo& info = sprites::kAnims[anim];
    return frame >= info.firstFrame && frame < info.firstFrame + info.frameCount;
}

static void TestAnchorsAreOnTheHead() {
    for (int i = 0; i < sprites::kFrameCount; i++) {
        const cat::Anchor& a = sprites::kFrameAnchors[i];
        CHECK(a.x >= 0 && a.x < cat::kSpriteSize && a.y >= 0 && a.y < cat::kSpriteSize);
        if (InAnim(i, sprites::kButterfly) || InAnim(i, sprites::kYarn) ||
            InAnim(i, sprites::kGift) || InAnim(i, sprites::kNote) || InAnim(i, sprites::kFish) ||
            InAnim(i, sprites::kHatWitch) || InAnim(i, sprites::kHatSanta) ||
            InAnim(i, sprites::kHatParty)) {
            continue;  // toys have no head
        }
        bool bellyUp = InAnim(i, sprites::kBelly) || (InAnim(i, sprites::kRoll) &&
                                                      i == sprites::kAnims[sprites::kRoll].firstFrame + 1);
        if (!IsAirborneFrame(i) || InAnim(i, sprites::kPounce) || InAnim(i, sprites::kDangle)) {
            CHECK(a.upright == !bellyUp);
        }
    }
}

static void TestWakeRoutine() {
    engine::World w = MakeWorld();
    int yawns = 0;
    for (uint32_t seed = 1; seed <= 60; seed++) {
        CatState s = engine::StartWakeUp(CatWith({90, 30, 50}, seed), 0);
        uint64_t now = 0;
        s = RunUntil(s, w, &now, 3000, [](const CatState& c) { return c.activity != Activity::WakeUp; });
        CHECK(s.activity == Activity::Stretch);
        s = RunUntil(s, w, &now, 3000, [](const CatState& c) { return c.activity != Activity::Stretch; });
        CHECK(s.activity == Activity::Yawn || s.activity == Activity::Sit);
        yawns += s.activity == Activity::Yawn;
        s = RunUntil(s, w, &now, 3000, [](const CatState& c) { return c.activity == Activity::Sit; });
        CHECK(s.activity == Activity::Sit);
    }
    CHECK(yawns > 10 && yawns < 50);
}

static void TestSleepShowsZzzAboveHead() {
    CatState s = engine::StartSleep(CatWith({30, 20, 50}), 0, false);
    CHECK(s.effect == engine::Effect::Zzz);
    engine::EffectDraw z = engine::CurrentEffect(s, 100);
    CHECK(InAnim(z.frame, sprites::kZzz));
    POINT head = engine::HeadOnCanvas(s, 100);
    CHECK(z.y < head.y);

    CatState woke = engine::OnClick(s, 200);
    CHECK(woke.effect == engine::Effect::None);
}

static void TestEffectsFollowTheHead() {
    CatState right = engine::StartPurr(CatWith({80, 30, 50}), 0, 1000);
    CatState left = right;
    left.dir = -1;
    engine::EffectDraw r = engine::CurrentEffect(right, 50);
    engine::EffectDraw l = engine::CurrentEffect(left, 50);
    CHECK(r.x > l.x);  // the head is on the right when facing right
    CHECK(r.y == l.y);
    // Hearts rise over time.
    CHECK(engine::CurrentEffect(right, 700).y < r.y);
}

static int CountChoice(Needs needs, Activity wanted, uint32_t seeds = 400) {
    int count = 0;
    for (uint32_t seed = 1; seed <= seeds; seed++) {
        CatState s = CatWith(needs, seed);
        s = engine::Step(s, s.untilMs, 0.0, MakeWorld());
        count += s.activity == wanted;
    }
    return count;
}

static void TestSelfCareHappens() {
    CHECK(CountChoice({80, 30, 50}, Activity::Groom) > 10);
    CHECK(CountChoice({80, 30, 50}, Activity::Scratch) > 3);

    engine::World w = MakeWorld();
    CatState groom = engine::StartGroom(CatWith({80, 30, 50}), 0);
    CHECK(groom.untilMs >= engine::kGroomMinMs && groom.untilMs <= engine::kGroomMaxMs);
    CatState after = engine::Step(groom, groom.untilMs, 0.0, w);
    CHECK(after.activity != Activity::Groom || after.startMs == groom.untilMs);
}

static void TestKneading() {
    engine::World w = MakeWorld();
    CatState bed = engine::StartKnead(CatWith({30, 30, 50}), 0, true);
    CHECK(bed.activity == Activity::Knead && bed.effect == engine::Effect::Hearts);
    CHECK(engine::Step(bed, bed.untilMs, 0.0, w).activity == Activity::LieDown);
    CatState happy = engine::StartKnead(CatWith({80, 30, 50}), 0, false);
    CHECK(engine::Step(happy, happy.untilMs, 0.0, w).activity == Activity::Sit);

    // Tired cats sometimes knead before napping.
    int kneads = 0;
    for (uint32_t seed = 1; seed <= 300; seed++) {
        CatState s = engine::StartNap(CatWith({20, 30, 50}, seed), 0);
        kneads += s.activity == Activity::Knead && s.napAfterKnead;
        CHECK(s.activity == Activity::Knead || s.activity == Activity::LieDown);
    }
    CHECK(kneads > 60 && kneads < 180);
}

static void TestChaseTail() {
    CHECK(CountChoice({80, 95, 50}, Activity::ChaseTail) > 5);
    CHECK(CountChoice({80, 30, 50}, Activity::ChaseTail) == 0);

    engine::World w = MakeWorld();
    CatState s = engine::StartChaseTail(CatWith({80, 95, 50}, 1, 300), 0);
    bool sawLeft = false, sawRight = false;
    uint64_t now = 0;
    s = RunUntil(s, w, &now, 10000, [&](const CatState& c) {
        sawLeft |= c.dir < 0;
        sawRight |= c.dir > 0;
        return c.activity != Activity::ChaseTail;
    });
    CHECK(sawLeft && sawRight);
    CHECK(s.activity == Activity::Sit && s.x == 300);
    CHECK(s.needs.boredom < 95);
}

static engine::World ToyWorld() {
    engine::World w = MakeWorld();
    w.toysEnabled = true;
    return w;
}

static void TestButterflyVisitsAndCrosses() {
    engine::World w = ToyWorld();
    CatState s = engine::SpawnButterfly(CatWith({80, 30, 50}), w, 0);
    CHECK(s.toy.kind == engine::ToyKind::Butterfly);
    bool fromLeft = s.toy.vx > 0;
    CHECK(fromLeft ? s.toy.x < w.minX : s.toy.x > w.maxX);

    // On its own it flutters (bobbing) across and leaves.
    engine::Toy t = s.toy;
    double lowest = 1e9, highest = -1e9;
    uint64_t now = 0;
    while (t.kind != engine::ToyKind::None && now < engine::kButterflyStaysMs) {
        now += 33;
        t = engine::StepToy(t, 0.033, w, now);
        if (t.kind != engine::ToyKind::None) {
            lowest = t.lift < lowest ? t.lift : lowest;
            highest = t.lift > highest ? t.lift : highest;
        }
    }
    CHECK(t.kind == engine::ToyKind::None);
    CHECK(highest - lowest > 5);
    CHECK(lowest > 0);

    // Spawns only with toys on and an awake cat.
    int spawned = 0;
    for (uint32_t seed = 1; seed <= 50; seed++) {
        CatState c = CatWith({80, 30, 50}, seed);
        spawned += engine::MaybeSpawnButterfly(c, w, 0, 3600).toy.kind != engine::ToyKind::None;
        CHECK(engine::MaybeSpawnButterfly(c, MakeWorld(), 0, 3600).toy.kind ==
              engine::ToyKind::None);
    }
    CHECK(spawned > 40);
    CatState sleeping = engine::StartSleep(CatWith({30, 30, 50}), 0, false);
    CHECK(engine::MaybeSpawnButterfly(sleeping, w, 0, 3600).toy.kind == engine::ToyKind::None);
}

static void TestCatHuntsButterfly() {
    engine::World w = ToyWorld();
    int caught = 0, escaped = 0;
    for (uint32_t seed = 1; seed <= 120; seed++) {
        CatState s = engine::SpawnButterfly(CatWith({80, 30, 50}, seed, 500), w, 0);
        s.toy.x = 600;  // a few widths to the right
        s.toy.vx = 0;
        uint64_t now = 0;
        s = RunUntil(s, w, &now, 2000, [](const CatState& c) { return c.activity == Activity::Crouch; });
        CHECK(s.activity == Activity::Crouch && s.huntingToy && s.dir == 1);
        s = RunUntil(s, w, &now, 5000, [](const CatState& c) {
            return c.activity == Activity::Land || c.activity == Activity::Meow;
        });
        caught += s.toy.kind == engine::ToyKind::None;
        escaped += s.toy.kind == engine::ToyKind::Butterfly && s.toy.fleeing;
    }
    CHECK(caught + escaped == 120);
    CHECK(caught > 15 && caught < 70);  // roughly kCatchChancePercent
}

static void TestCaughtButterflyMakesCatProud() {
    engine::World w = ToyWorld();
    CatState s = engine::SpawnButterfly(CatWith({80, 30, 50}, 1, 500), w, 0);
    s.toy.x = 500;
    s = engine::StartLand(s, 0);
    s.huntingToy = true;
    for (uint32_t seed = 1; seed < 500 && s.toy.kind != engine::ToyKind::None; seed++) {
        CatState attempt = s;
        attempt.rng = seed;
        attempt = engine::ResolveToyHunt(attempt, w, 0);
        if (attempt.toy.kind == engine::ToyKind::None) {
            s = attempt;
        }
    }
    CHECK(s.toy.kind == engine::ToyKind::None && s.proud);
    s = engine::Step(s, engine::kLandMs, 0.03, w);
    CHECK(s.activity == Activity::Meow && s.effect == engine::Effect::Hearts);
}

static void TestYarnPlay() {
    engine::World w = MakeWorld();
    CatState s = engine::OnGiveYarn(CatWith({80, 40, 50}, 1, 300), w, 0);
    CHECK(s.toy.kind == engine::ToyKind::Yarn);
    CHECK(s.toy.x == 300 + cat::kSpriteSize * 2);

    int bats = 0;
    double furthest = 0;
    uint64_t now = 0;
    Activity last = s.activity;
    for (int i = 0; i < 30000 / 33; i++) {
        now += 33;
        s = engine::Step(s, now, 0.033, w);
        if (s.activity == Activity::Bat && last != Activity::Bat) {
            bats++;
        }
        last = s.activity;
        double travelled = fabs(s.toy.x - (300 + cat::kSpriteSize * 2));
        furthest = travelled > furthest ? travelled : furthest;
        CHECK(s.toy.x >= w.minX && s.toy.x <= w.maxX);
    }
    CHECK(bats >= 3);       // keeps batting it around
    CHECK(furthest > 100);  // and the ball really rolls

    // Done playing: the cat stops chasing, the ball eventually goes away.
    s.toy.playUntilMs = now;
    CatState after = Run(s, w, now, 10000, [](const CatState&) { return false; });
    CHECK(after.toy.kind == engine::ToyKind::Yarn || after.toy.kind == engine::ToyKind::None);
    engine::Toy ball = s.toy;
    ball = engine::StepToy(ball, 0.033, w, ball.untilMs);
    CHECK(ball.kind == engine::ToyKind::None);

    CatState sleeping = engine::StartSleep(CatWith({30, 30, 50}), 0, false);
    CHECK(engine::OnGiveYarn(sleeping, w, 0).toy.kind == engine::ToyKind::None);
}

static void TestYarnRollsAndBounces() {
    engine::World w = MakeWorld();
    engine::Toy ball = {};
    ball.kind = engine::ToyKind::Yarn;
    ball.x = 950;
    ball.vx = 300;
    ball.untilMs = 100000;
    bool bounced = false;
    for (int i = 0; i < 200; i++) {
        ball = engine::StepToy(ball, 0.033, w, 1000);
        bounced |= ball.vx < 0;
        CHECK(ball.x <= w.maxX);
    }
    CHECK(bounced);
    CHECK(ball.vx == 0);  // friction stops it
}

static void TestToyClicks() {
    engine::World w = MakeWorld();
    w.cursorX = 300;
    CatState s = engine::OnGiveYarn(CatWith({80, 40, 50}, 1, 300), w, 0);
    CatState flicked = engine::OnToyClicked(s, w, 10);
    CHECK(flicked.toy.vx > 0);  // away from the cursor on its left

    CatState gift = engine::LeaveGift(CatWith({80, 40, 50}, 1, 300), w, 0);
    CHECK(gift.toy.kind == engine::ToyKind::Gift && gift.activity == Activity::Meow);
    CHECK(gift.toy.x < 300);  // left behind the cat (facing right)
    CatState collected = engine::OnToyClicked(gift, w, 10);
    CHECK(collected.toy.kind == engine::ToyKind::None && collected.gifts == 1);

    CatState butterfly = engine::SpawnButterfly(CatWith({80, 40, 50}), ToyWorld(), 0);
    CHECK(engine::OnToyClicked(butterfly, w, 10).toy.fleeing);
}

static void TestGiftsAfterStrolls() {
    engine::World w = ToyWorld();
    int gifts = 0;
    for (uint32_t seed = 1; seed <= 2000; seed++) {
        CatState s = engine::StartWalk(CatWith({80, 40, 50}, seed, 500), w, 0, false);
        s.x = s.targetX;
        s = engine::Step(s, 33, 0.0, w);
        gifts += s.toy.kind == engine::ToyKind::Gift;
    }
    CHECK(gifts > 20 && gifts < 120);  // roughly kGiftChancePercent
    // Gift expires.
    CatState g = engine::LeaveGift(CatWith({80, 40, 50}), w, 0);
    CHECK(engine::StepToy(g.toy, 0.03, w, engine::kGiftStaysMs).kind == engine::ToyKind::None);
}

static void TestToyFrames() {
    engine::World w = ToyWorld();
    CatState none = CatWith({80, 40, 50});
    CHECK(engine::CurrentToyFrame(none, 0, 1).frame == -1);
    CatState b = engine::SpawnButterfly(none, w, 0);
    engine::ToyDraw d = engine::CurrentToyFrame(b, 50, 1);
    CHECK(InAnim(d.frame, sprites::kButterfly));
    CHECK(d.mirror == (b.toy.vx < 0));
    CatState y = engine::OnGiveYarn(none, w, 0);
    CHECK(InAnim(engine::CurrentToyFrame(y, 0, 1).frame, sprites::kYarn));
    CatState g = engine::LeaveGift(none, w, 0);
    CHECK(InAnim(engine::CurrentToyFrame(g, 0, 1).frame, sprites::kGift));
}

static void TestSeasonalHats() {
    using cat::Hat;
    CHECK(cat::SeasonalHat(10, 18, 0, 0) == Hat::Witch);
    CHECK(cat::SeasonalHat(10, 31, 0, 0) == Hat::Witch);
    CHECK(cat::SeasonalHat(10, 14, 0, 0) == Hat::None);
    CHECK(cat::SeasonalHat(11, 1, 0, 0) == Hat::None);
    CHECK(cat::SeasonalHat(12, 1, 0, 0) == Hat::Santa);
    CHECK(cat::SeasonalHat(12, 26, 0, 0) == Hat::Santa);
    CHECK(cat::SeasonalHat(12, 27, 0, 0) == Hat::None);
    CHECK(cat::SeasonalHat(12, 31, 0, 0) == Hat::Party);
    CHECK(cat::SeasonalHat(1, 1, 0, 0) == Hat::Party);
    CHECK(cat::SeasonalHat(6, 9, 6, 9) == Hat::Party);    // birthday
    CHECK(cat::SeasonalHat(10, 20, 10, 20) == Hat::Party);  // birthday beats Halloween
    CHECK(cat::SeasonalHat(6, 9, 0, 0) == Hat::None);
}

static void TestMealKeys() {
    CHECK(cat::MealKey(100, 8) == 200);
    CHECK(cat::MealKey(100, 19) == 201);
    CHECK(cat::MealKey(100, 9) == -1);
    CHECK(cat::MealKey(100, 18) == -1);
    CHECK(cat::MealKey(101, 8) != cat::MealKey(100, 8));
}

// Feeds a detector a synthetic signal: a beat every beatMs over a quieter bed.
static cat::MusicDetector FeedMusic(cat::MusicDetector m, uint64_t* now, uint64_t ms,
                                    uint64_t beatMs, double bed, double beatPeak) {
    for (uint64_t t = 0; t < ms; t += 33) {
        *now += 33;
        bool onBeat = beatMs > 0 && (*now % beatMs) < 33;
        m = cat::UpdateMusic(m, onBeat ? beatPeak : bed, 0.033, *now);
    }
    return m;
}

static void TestMusicDetection() {
    uint64_t now = 1000;
    cat::MusicDetector m = {};
    m = FeedMusic(m, &now, 2000, 500, 0.15, 0.6);
    CHECK(!m.playing);  // too short so far
    m = FeedMusic(m, &now, 4000, 500, 0.15, 0.6);
    CHECK(m.playing);
    CHECK(m.beatMs > 400 && m.beatMs < 600);

    m = FeedMusic(m, &now, 1500, 0, 0.0, 0.0);
    CHECK(m.playing);  // a short pause doesn't stop it
    m = FeedMusic(m, &now, 2000, 0, 0.0, 0.0);
    CHECK(!m.playing && m.beatMs == 0);

    // A one-second notification sound is not music.
    cat::MusicDetector ding = FeedMusic({}, &now, 1000, 0, 0.5, 0.5);
    ding = FeedMusic(ding, &now, 3000, 0, 0.0, 0.0);
    CHECK(!ding.playing);
}

static void TestDanceTempo() {
    CHECK(engine::DancePercent(0) == 100);
    CHECK(engine::DancePercent(600) == 100);
    CHECK(engine::DancePercent(300) == 200);
    CHECK(engine::DancePercent(150) == engine::kMaxDancePercent);
    CHECK(engine::DancePercent(5000) == engine::kMinDancePercent);
}

static void TestDancesToMusic() {
    engine::World music = MakeWorld();
    music.musicPlaying = true;
    music.beatMs = 500;
    CatState s = engine::Step(CatWith({80, 30, 50}), 10, 0.03, music);
    CHECK(s.activity == Activity::Dance && s.effect == engine::Effect::Notes);
    CHECK(s.playbackPercent == 120);
    CHECK(wcscmp(engine::MoodName(s), L"vibing") == 0);
    engine::EffectDraw note = engine::CurrentEffect(s, 100);
    CHECK(InAnim(note.frame, sprites::kNote));

    // Walking cats finish their walk; sleeping cats sleep through it.
    CatState walking = engine::StartWalk(CatWith({80, 30, 50}), music, 0, false);
    CHECK(engine::Step(walking, 10, 0.03, music).activity == Activity::Walk);
    CatState sleeping = engine::StartSleep(CatWith({30, 30, 50}), 0, false);
    CHECK(engine::Step(sleeping, 10, 0.03, music).activity == Activity::Sleep);

    // Music stops: it sits back down.
    s = engine::Step(s, 2000, 0.03, MakeWorld());
    CHECK(s.activity == Activity::Sit);
}

static void TestMealtime() {
    engine::World hungry = MakeWorld();
    hungry.hungry = true;
    CatState s = engine::Step(CatWith({80, 30, 50}), 10, 0.03, hungry);
    CHECK(s.effect == engine::Effect::Fish && s.hungry);
    CHECK(wcscmp(engine::MoodName(s), L"hungry") == 0);
    CHECK(InAnim(engine::CurrentEffect(s, 100).frame, sprites::kFish));

    // Fed (or the meal hour passed): the fish goes away.
    s = engine::Step(s, 50, 0.03, MakeWorld());
    CHECK(s.effect == engine::Effect::None && !s.hungry);

    // Hungry cats meow a lot more.
    int meows = 0, calm = 0;
    for (uint32_t seed = 1; seed <= 300; seed++) {
        CatState a = CatWith({80, 30, 50}, seed);
        meows += engine::Step(a, a.untilMs, 0.0, hungry).activity == Activity::Meow;
        calm += engine::Step(a, a.untilMs, 0.0, MakeWorld()).activity == Activity::Meow;
    }
    CHECK(meows > calm + 30);
}

static void TestHatTurnsWithTheCat() {
    static uint32_t catFrame[kPixels];
    static uint32_t hat[kPixels];
    static uint32_t canvas[cat::kCanvasWidth * cat::kCanvasHeight];
    hat[2 * cat::kSpriteSize + 3] = 0xFF00FF00;  // one pixel at (3, 2)
    cat::ComposeCanvas(catFrame, false, hat, 10, -1, nullptr, 0, 0, canvas);
    CHECK(canvas[(1 + cat::kEffectHeadroom) * cat::kCanvasWidth + 13] == 0xFF00FF00);
    cat::ComposeCanvas(catFrame, true, hat, 10, -1, nullptr, 0, 0, canvas);
    CHECK(canvas[(1 + cat::kEffectHeadroom) * cat::kCanvasWidth + (cat::kSpriteSize - 1 - 13)] ==
          0xFF00FF00);
    // Hats can poke into the headroom above the sprite.
    cat::ComposeCanvas(catFrame, false, hat, 10, -10, nullptr, 0, 0, canvas);
    CHECK(canvas[(cat::kEffectHeadroom - 8) * cat::kCanvasWidth + 13] == 0xFF00FF00);
}

static int Peak(const std::vector<int16_t>& v) {
    int peak = 0;
    for (int16_t x : v) {
        peak = abs(x) > peak ? abs(x) : peak;
    }
    return peak;
}

// Sign changes per second over a slice: a rough pitch measure.
static double CrossingsPerSecond(const std::vector<int16_t>& v, double from, double to) {
    size_t a = (size_t)(v.size() * from), b = (size_t)(v.size() * to);
    int crossings = 0;
    for (size_t i = a + 1; i < b; i++) {
        crossings += (v[i - 1] < 0) != (v[i] < 0);
    }
    return crossings / ((double)(b - a) / sound::kSampleRate);
}

static void SaveForListening(const char* name, const std::vector<uint8_t>& wav) {
    char path[260];
    snprintf(path, sizeof(path), "build/%s.wav", name);
    FILE* f = fopen(path, "wb");
    if (f) {
        fwrite(wav.data(), 1, wav.size(), f);
        fclose(f);
    }
}

static void TestWavHeader() {
    std::vector<int16_t> samples = {0, 1000, -1000};
    std::vector<uint8_t> wav = sound::MakeWav(samples);
    CHECK(wav.size() == 44 + 6);
    CHECK(memcmp(wav.data(), "RIFF", 4) == 0 && memcmp(wav.data() + 8, "WAVEfmt ", 8) == 0);
    CHECK(memcmp(wav.data() + 36, "data", 4) == 0);
    CHECK(wav[24] == (sound::kSampleRate & 0xFF) && wav[34] == 16);
    CHECK(wav[40] == 6 && wav[44 + 2] == (uint8_t)(1000 & 0xFF));
}

static void TestMeowSound() {
    std::vector<int16_t> meow = sound::SynthMeow(1.0, 1.0, 0.5);
    CHECK(meow.size() == (size_t)(sound::kSampleRate * 0.55));
    CHECK(Peak(meow) > 3000 && Peak(meow) <= 32767);
    CHECK(abs(meow.front()) < 200 && abs(meow.back()) < 200);  // no clicks
    // "Mi-aow": higher in the middle than at the end.
    CHECK(CrossingsPerSecond(meow, 0.3, 0.45) > CrossingsPerSecond(meow, 0.8, 0.95));

    std::vector<int16_t> quiet = sound::SynthMeow(1.0, 1.0, 0.1);
    CHECK(Peak(quiet) < Peak(meow));
    CHECK(sound::SynthMeow(1.0, 1.0, 0.0).size() == meow.size() && Peak(sound::SynthMeow(1.0, 1.0, 0.0)) == 0);
    CHECK(sound::SynthMeow(1.1, 0.8, 0.5).size() < meow.size());

    SaveForListening("meow", sound::MakeWav(meow));
    SaveForListening("meow_short", sound::MakeWav(sound::SynthMeow(1.12, 0.8, 0.5)));
    SaveForListening("meow_long", sound::MakeWav(sound::SynthMeow(0.92, 1.25, 0.5)));
}

static void TestTrillAndPurr() {
    std::vector<int16_t> trill = sound::SynthTrill(0.5);
    CHECK(Peak(trill) > 2000);
    CHECK(trill.size() < sound::SynthMeow(1.0, 1.0, 0.5).size());

    std::vector<int16_t> purr = sound::SynthPurr(0.5, 0x5EED);
    CHECK(Peak(purr) > 1000);
    CHECK(abs(purr.front()) < 200 && abs(purr.back()) < 200);  // loops cleanly
    // A purr is a rumble: far fewer sign changes than a meow.
    CHECK(CrossingsPerSecond(purr, 0.1, 0.9) <
          CrossingsPerSecond(sound::SynthMeow(1.0, 1.0, 0.5), 0.1, 0.9));
    CHECK(sound::SynthPurr(0.5, 0x5EED) == purr);  // same seed, same purr

    SaveForListening("trill", sound::MakeWav(trill));
    SaveForListening("purr", sound::MakeWav(purr));
}

static void TestTickRate() {
    CatState sitting = CatWith({80, 30, 50});
    CHECK(engine::TickIntervalMs(sitting) == engine::kCalmTickMs);
    CHECK(engine::TickIntervalMs(engine::StartSleep(sitting, 0, false)) == engine::kSleepTickMs);
    CHECK(engine::TickIntervalMs(engine::StartWalk(sitting, MakeWorld(), 0, false)) ==
          engine::kFastTickMs);
    CHECK(engine::TickIntervalMs(engine::OnPickUp(sitting, 0)) == engine::kFastTickMs);
    // A moving toy keeps things smooth even while the cat sits.
    CHECK(engine::TickIntervalMs(engine::OnGiveYarn(sitting, MakeWorld(), 0)) ==
          engine::kFastTickMs);
    CHECK(engine::TickIntervalMs(engine::LeaveGift(sitting, MakeWorld(), 0)) ==
          engine::kCalmTickMs);  // a gift doesn't move, and meowing is calm
}

static void TestHuntChanceIndependentOfTickRate() {
    // Same racing cursor, ticks of 33 ms vs 100 ms: similar time to the first crouch.
    auto averageMs = [](double tickSeconds) {
        uint64_t total = 0;
        for (uint32_t seed = 1; seed <= 60; seed++) {
            CatState s = CatWith({80, 30, 50}, seed, 500);
            engine::World w = CursorWorld(650, 2000);
            uint64_t now = 0;
            uint64_t stepMs = (uint64_t)(tickSeconds * 1000);
            while (s.activity != Activity::Crouch && now < 60000) {
                now += stepMs;
                s = engine::Step(s, now, tickSeconds, w);
            }
            total += now;
        }
        return total / 60.0;
    };
    double fast = averageMs(0.033), slow = averageMs(0.1);
    CHECK(fast > 200 && slow > 200);
    CHECK(slow < fast * 2.0 && fast < slow * 2.0);
}

static void TestPutYarnAway() {
    engine::World w = MakeWorld();
    CatState s = engine::OnGiveYarn(CatWith({80, 40, 50}, 1, 300), w, 0);
    s = Run(s, w, 0, 3000, [](const CatState& c) { return c.activity == Activity::Trot; });
    CHECK(s.activity == Activity::Trot);
    CatState away = engine::OnPutYarnAway(s, 3000);
    CHECK(away.toy.kind == engine::ToyKind::None);
    CHECK(away.activity == Activity::Sit);  // stops chasing
    // Nothing to put away: nothing changes.
    CatState gift = engine::LeaveGift(CatWith({80, 40, 50}), w, 0);
    CHECK(engine::OnPutYarnAway(gift, 10).toy.kind == engine::ToyKind::Gift);
}

static void TestDates() {
    CHECK(cat::DaysFromCivil(1970, 1, 1) == 0);
    CHECK(cat::DaysFromCivil(2026, 10, 18) - cat::DaysFromCivil(2026, 10, 17) == 1);
    CHECK(cat::DaysFromCivil(2027, 3, 1) - cat::DaysFromCivil(2027, 2, 28) == 1);
    CHECK(cat::DaysFromCivil(2028, 3, 1) - cat::DaysFromCivil(2028, 2, 28) == 2);  // leap year
    int m = 0, d = 0;
    cat::CivilMonthDay(cat::DaysFromCivil(2026, 10, 17), &m, &d);
    CHECK(m == 10 && d == 17);
    cat::CivilMonthDay(cat::DaysFromCivil(2028, 2, 29), &m, &d);
    CHECK(m == 2 && d == 29);

    CHECK(cat::ParseDate(L"2026-10-17") == cat::DaysFromCivil(2026, 10, 17));
    CHECK(cat::ParseDate(L"") == 0);
    CHECK(cat::ParseDate(L"soon") == 0);
    CHECK(cat::ParseDate(L"2026-13-01") == 0);

    int birthday = cat::DaysFromCivil(2026, 10, 17);
    CHECK(cat::DayOfLife(birthday, birthday) == 1);
    CHECK(cat::DayOfLife(birthday, cat::DaysFromCivil(2026, 10, 18)) == 2);
    CHECK(cat::DayOfLife(birthday, birthday - 3) == 1);  // birthday in the future
}

static void TestStayHere() {
    engine::World still = MakeWorld(kDayHour, 0, true);
    // Yarn in reach still gets batted; yarn further off is only watched.
    CatState s = engine::OnGiveYarn(CatWith({80, 40, 50}, 1, 300), still, 0);
    s.toy.x = 310;
    CHECK(engine::Step(s, 33, 0.033, still).activity == Activity::Bat);
    s.toy.x = 600;
    CatState watched = Run(s, still, 0, 5000, [](const CatState&) { return false; });
    CHECK(watched.x == 300);
    // Butterflies are watched, not hunted.
    CatState b = engine::SpawnButterfly(CatWith({80, 40, 50}, 1, 500), ToyWorld(), 0);
    b.toy.x = 600;
    b.toy.vx = 0;
    engine::World stillToys = still;
    stillToys.toysEnabled = true;
    CatState after = Run(b, stillToys, 0, 3000, [](const CatState&) { return false; });
    CHECK(after.activity != Activity::Crouch && after.activity != Activity::Pounce);
    CHECK(after.x == 500);
    // It still naps when you're away, and reacts to new windows.
    engine::World away = still;
    away.userIdleSeconds = engine::kIdleNapSeconds;
    CHECK(engine::Step(CatWith({80, 40, 50}), 10, 0.03, away).activity == Activity::LieDown);
    CHECK(engine::OnWindowOpened(CatWith({80, 40, 50}), 10).effect == engine::Effect::Alert);
}

static void TestInviteButterfly() {
    engine::World w = ToyWorld();
    CatState s = engine::OnInviteButterfly(CatWith({80, 40, 50}), w, 0);
    CHECK(s.toy.kind == engine::ToyKind::Butterfly);
    CatState yarn = engine::OnGiveYarn(CatWith({80, 40, 50}), w, 0);
    CHECK(engine::OnInviteButterfly(yarn, w, 0).toy.kind == engine::ToyKind::Butterfly);
    CatState gift = engine::LeaveGift(CatWith({80, 40, 50}), w, 0);
    CHECK(engine::OnInviteButterfly(gift, w, 0).toy.kind == engine::ToyKind::Gift);
}

int main() {
    TestComputeScale();
    TestPremultiply();
    TestDecodeRleFrame();
    TestGeneratedSprites();
    TestFrameInAnim();
    TestWindowOrigin();
    TestTaskbarGeometry();
    TestStartsSittingWithTailFlick();
    TestNeedsDrift();
    TestNightIsHoursElevenToSeven();
    TestExhaustedCatAlwaysNaps();
    TestBoredCatMovesMore();
    TestTiredCatNapsMoreAtNight();
    TestWalkStaysInBoundsAndArrives();
    TestTrotIsFasterThanWalk();
    TestNapRestoresEnergyThenWakes();
    TestSleepsWhileUserAwayAndGreetsOnReturn();
    TestStayStillNeverMoves();
    TestClampsToShrunkenTaskbar();
    TestPetting();
    TestLongDayStaysSane();
    TestComposeCanvas();
    TestHoverPurrsWithHearts();
    TestLooksAtNearbyCursor();
    TestFastCursorTriggersHuntAndPounce();
    TestNoHuntWhenSlowTiredOrClose();
    TestPounceReachIsLimited();
    TestDoubleClickBellyRoll();
    TestPickUpCarryDropLand();
    TestMenuCommands();
    TestClingyCatSeeksCursor();
    TestWindowOpenedAlertsOrStartles();
    TestStartleHopsAndLands();
    TestFlashingButtonSendsCatOver();
    TestGreetsYouAfterIdleNap();
    TestUnlockSaysHello();
    TestRoamRange();
    TestActivityLevel();
    TestSettingsSwitchesInTheWorld();
    TestMoodNames();
    TestRestoreNeeds();
    TestAnchorsAreOnTheHead();
    TestWakeRoutine();
    TestSleepShowsZzzAboveHead();
    TestEffectsFollowTheHead();
    TestSelfCareHappens();
    TestKneading();
    TestChaseTail();
    TestButterflyVisitsAndCrosses();
    TestCatHuntsButterfly();
    TestCaughtButterflyMakesCatProud();
    TestYarnPlay();
    TestYarnRollsAndBounces();
    TestToyClicks();
    TestGiftsAfterStrolls();
    TestToyFrames();
    TestSeasonalHats();
    TestMealKeys();
    TestMusicDetection();
    TestDanceTempo();
    TestDancesToMusic();
    TestMealtime();
    TestHatTurnsWithTheCat();
    TestWavHeader();
    TestMeowSound();
    TestTrillAndPurr();
    TestTickRate();
    TestHuntChanceIndependentOfTickRate();
    TestPutYarnAway();
    TestDates();
    TestStayHere();
    TestInviteButterfly();
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
