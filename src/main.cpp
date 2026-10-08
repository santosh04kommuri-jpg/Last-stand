/*
 * 2D Wave Survival Game - Smoke Test / Project Scaffold
 * Toolchain: w64devkit GCC 15.2.0 + raylib 6.0 (bundled in C:\raylib\w64devkit)
 *
 * Purpose: prove the toolchain and asset paths work before building gameplay.
 * Run with --frames N to auto-exit after N frames (for scripted verification).
 */
#include "raylib.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <vector>

// Asset root: the pack folder is doubly nested on disk; do not rename/move assets.
#define ASSET_ROOT "2D Stylized Adventure Game Asset Pack/2D Stylized Adventure Game Asset Pack/"

// Standard on-screen character heights in pixels - the enemy towers over the player.
const float PLAYER_HEIGHT = 150.0f;
const float ENEMY_HEIGHT  = 200.0f;

// 2.5D horizon layout: sky fills the top 40% of the screen, the rocky platform
// fills the rest. The horizon Y is computed from the screen size at startup;
// these are the horizontal world bounds of the rock platform.
const float ARENA_MIN_X = -2000.0f;
const float ARENA_MAX_X =  2000.0f;

// Session stats (antivirus terminal theme)
int totalKills = 0;           // malware purged across the whole session
int weaponLevel = 1;          // 1-to-1 upgrade: +1 on every confirmed kill
float upgradeNoticeTimer = 0.0f; // PAYLOAD UPGRADED visibility timer (seconds)

// Cybersecurity CYAN style (raylib has no CYAN built-in)
static const Color CYBER_CYAN = { 0, 220, 255, 255 };

// Animated enemy: 6 individual walk + attack frame images (white keyed out).
struct Enemy {
    Vector2 position;          // WORLD position = feet (bottom-center anchor)
    Texture2D walkFrames[6];   // individual frame textures (no sprite sheet)
    Texture2D attackFrames[6];
    Texture2D defeatFrames[6];
    int currentFrame;          // 0..5
    float frameTimer;          // accumulates GetFrameTime()
    float frameSpeed;          // seconds per frame (0.1f = 10 FPS)
    float speed;               // chase speed (pixels/second)
    int facingDirection = 1;   // 1 = right, -1 = left
    int hp = 30;               // 3 player hits of 10 damage
    bool isAttacking = false;
    float attackTimer = 0.0f;  // seconds since attack started (~0.4s long)
    float attackCooldown = 0.0f; // seconds until next hit may land (1.5s)
    float damageFlashTimer = 0.0f;  // red tint while > 0
    float knockbackVelocity = 0.0f; // horizontal shove (px/s), bleeds off with friction
    bool isDefeated = false;    // collapsing: defeat animation + despawn delay
    float defeatTimer = 0.0f;   // seconds resting on the ground before erase
};

// Player: 6 individual walk + attack frame images.
struct Player {
    Vector2 position;          // WORLD position = feet (bottom-center anchor)
    Texture2D walkFrames[6];   // individual frame textures (no sprite sheet)
    Texture2D attackFrames[6];
    Texture2D defeatFrames[6];
    int currentFrame;          // 0..5
    float frameTimer;          // accumulates GetFrameTime()
    float frameSpeed;          // seconds per frame
    float moveSpeed;           // pixels per second (WASD)
    bool moving;               // WASD input active this frame
    int facingDirection = 1;   // 1 = right, -1 = left (horizontal flip)
    int hp = 100;              // 4 enemy hits of 25 damage
    int lives = 3;             // respawns left before true game over
    bool isAttacking = false;
    float attackTimer = 0.0f;  // seconds since attack started (~0.4s long)
    float damageFlashTimer = 0.0f;  // red tint while > 0
    float knockbackVelocity = 0.0f; // horizontal shove (px/s), bleeds off with friction
};

// Load one animation frame: strip near-white background, upload as texture.
// Compression leaves off-white static/noise around the sprites, so any pixel
// with R, G and B all above 240 becomes fully transparent (raylib's built-in
// color-key has no tolerance, hence this manual nested loop).
static bool LoadFrame(Texture2D* out, const char* path)
{
    *out = Texture2D{};
    Image img = LoadImage(path);
    if (img.data == NULL) return false;

    Color* pixels = LoadImageColors(img);   // fresh RGBA copy of the pixel data
    if (pixels != NULL) {
        for (int y = 0; y < img.height; y++) {
            for (int x = 0; x < img.width; x++) {
                Color* p = &pixels[y * img.width + x];
                if (p->r > 240 && p->g > 240 && p->b > 240) {
                    p->r = 0; p->g = 0; p->b = 0; p->a = 0;   // transparent
                }
            }
        }
        Image keyed = { pixels, img.width, img.height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
        *out = LoadTextureFromImage(keyed);
        UnloadImageColors(pixels);
    } else {
        *out = LoadTextureFromImage(img);    // fallback: load unprocessed
    }
    UnloadImage(img);
    return out->id != 0;
}

// Tile a texture across a rectangle, clipping the last column/row at the edges.
// NOTE: raylib 6.0 has no DrawTextureTiled(), so we loop DrawTexturePro here.
static void DrawGroundTiled(Texture2D tex, Rectangle area)
{
    if (tex.id == 0 || tex.width <= 0 || tex.height <= 0) return;
    const float tw = (float)tex.width;
    const float th = (float)tex.height;
    for (float y = area.y; y < area.y + area.height; y += th) {
        for (float x = area.x; x < area.x + area.width; x += tw) {
            Rectangle src = { 0.0f, 0.0f, tw, th };
            Rectangle dst = { x, y, tw, th };
            if (x + tw > area.x + area.width)  { src.width  = area.x + area.width  - x; dst.width  = src.width; }
            if (y + th > area.y + area.height) { src.height = area.y + area.height - y; dst.height = src.height; }
            DrawTexturePro(tex, src, dst, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
        }
    }
}

// Trap a position on the rocks: never above minY (the horizon), never below
// the bottom of the visible band, never past the platform's horizontal edges.
static void ClampToRocks(Vector2* pos, float minY, float maxY)
{
    if (pos->x < ARENA_MIN_X) pos->x = ARENA_MIN_X;
    if (pos->x > ARENA_MAX_X) pos->x = ARENA_MAX_X;
    if (pos->y < minY)        pos->y = minY;   // don't walk up into the sky
    if (pos->y > maxY)        pos->y = maxY;   // stay on the platform / on screen
}

// Shove a position horizontally and bleed the velocity off with friction
// (1500 px/s^2 deceleration toward zero). Caller clamps arena bounds.
static void ApplyKnockback(Vector2* pos, float* vel, float dt)
{
    if (*vel == 0.0f) return;
    pos->x += *vel * dt;
    const float step = 1500.0f * dt;
    if (*vel > 0.0f) { *vel -= step; if (*vel < 0.0f) *vel = 0.0f; }
    else             { *vel += step; if (*vel > 0.0f) *vel = 0.0f; }
}

int main(int argc, char** argv)
{
    int autoExitFrames = -1;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && (i + 1) < argc) {
            autoExitFrames = std::atoi(argv[i + 1]);
        }
    }

    SetTraceLogLevel(LOG_WARNING);
    InitWindow(1280, 720, "Wave Survival - Smoke Test");
    SetTargetFPS(60);

    // NOTE: filenames contain intentional typos/spaces from the asset pack
    // (e.g. "Sky.jpg"); paths must match disk exactly.
    const char* skyPath = ASSET_ROOT "Enviroment/Sky.jpg";

    Texture2D sky = LoadTexture(skyPath);

    const bool skyOk = sky.id != 0 && sky.width > 0;

    // Environment ground tile used to fill the entire arena
    Texture2D ground = LoadTexture(ASSET_ROOT "Enviroment/Tile/Ground_A.png");
    const bool groundOk = ground.id != 0 && ground.width > 0;

    // Health bar UI from the asset pack (sic: double-dot filename)
    Texture2D healthBarTex = LoadTexture(ASSET_ROOT "UI/Health_Bar..png");
    const bool healthBarOk = healthBarTex.id != 0;

    // Interactive HUD controls - all three already carry alpha channels, so no
    // white-background keying is required (plain LoadTexture is correct here).
    Texture2D uiArrowActive   = LoadTexture(ASSET_ROOT "UI/Right_1.png"); // bright arrow
    Texture2D uiArrowInactive = LoadTexture(ASSET_ROOT "UI/Right_2.png"); // dim arrow
    Texture2D uiAction        = LoadTexture(ASSET_ROOT "UI/stop.png");    // attack button
    const bool uiOk = uiArrowActive.id != 0 && uiArrowInactive.id != 0 && uiAction.id != 0;

    // Individual walk frames (6 per character), names exactly as found on disk:
    const char* enemyFramePaths[6] = {
        ASSET_ROOT "enemy/primary huntur 1.png",   // sic: "huntur" + space
        ASSET_ROOT "enemy/primary huntur 2.png",
        ASSET_ROOT "enemy/primary huntur 3.png",
        ASSET_ROOT "enemy/primary huntur 4.png",
        ASSET_ROOT "enemy/primary hunter5.png",    // sic: no space
        ASSET_ROOT "enemy/primary hunter6.png",
    };
    const char* playerFramePaths[6] = {
        ASSET_ROOT "enemy/main player1.png",       // sic: no space before 1
        ASSET_ROOT "enemy/main player 2.png",
        ASSET_ROOT "enemy/main player 3.png",
        ASSET_ROOT "enemy/main player 4.png",
        ASSET_ROOT "enemy/main player 5.png",
        ASSET_ROOT "enemy/main player 6.png",
    };

    // Attack frames - hand-sliced. Disk reality: ONE folder "player attacks"
    // with 5 frames each (frame 6 not present yet; path kept so it auto-loads
    // if it is added later - a missing file loads as a harmless blank).
    const char* playerAttackPaths[6] = {
        ASSET_ROOT "player attacks/player attacks 1.png",  // sic: "attacks" plural
        ASSET_ROOT "player attacks/player attack 2.png",
        ASSET_ROOT "player attacks/player attack 3.png",
        ASSET_ROOT "player attacks/player attack 4.png",
        ASSET_ROOT "player attacks/player attack 5png.png", // sic: missing dot
        ASSET_ROOT "player attacks/player attack 6.png",    // not on disk yet
    };
    const char* enemyAttackPaths[6] = {
        ASSET_ROOT "player attacks/enemy attack 1.png",
        ASSET_ROOT "player attacks/enemy attack 2.png",
        ASSET_ROOT "player attacks/enemy attack 3.png",
        ASSET_ROOT "player attacks/enemy attack 4.png",
        ASSET_ROOT "player attacks/enemy attack 5.png",
        ASSET_ROOT "player attacks/enemy attack 6.png",     // not on disk yet
    };

    // Defeat (knockout) frames - expected at project root per spec:
    // assets/player_defeat/1.png .. 6.png. Files NOT yet on disk; each missing
    // file loads as a harmless blank (draw falls back to the walk frame), and
    // the system starts working the moment the files are dropped in.
    const char* playerDefeatPaths[6] = {
        "assets/player_defeat/1.png", "assets/player_defeat/2.png",
        "assets/player_defeat/3.png", "assets/player_defeat/4.png",
        "assets/player_defeat/5.png", "assets/player_defeat/6.png",
    };

    // Enemy defeat (collapse) frames - same drop-in convention as the player:
    // missing files load as blanks (draw falls back to walk), and the system
    // activates automatically once the files are saved to disk.
    const char* enemyDefeatPaths[6] = {
        "assets/enemy_defeat/1.png", "assets/enemy_defeat/2.png",
        "assets/enemy_defeat/3.png", "assets/enemy_defeat/4.png",
        "assets/enemy_defeat/5.png", "assets/enemy_defeat/6.png",
    };

    // --- Enemy initialization -------------------------------------------
    // Textures load ONCE into a shared holder; every vector element copies the
    // texture HANDLES (same GPU texture, unloaded once at the end).
    Enemy enemyAssets = {};
    enemyAssets.currentFrame = 0;
    enemyAssets.frameTimer   = 0.0f;
    enemyAssets.frameSpeed   = 0.1f;     // 0.1s per frame = 10 FPS walk cycle
    enemyAssets.speed        = 110.0f;   // slightly slower than the player (200)

    bool enemyOk = true;
    for (int i = 0; i < 6; i++) {
        if (!LoadFrame(&enemyAssets.walkFrames[i], enemyFramePaths[i])) enemyOk = false;
    }
    for (int i = 0; i < 6; i++) {
        LoadFrame(&enemyAssets.attackFrames[i], enemyAttackPaths[i]); // frame 6 optional
    }
    const bool enemyAttackOk = enemyAssets.attackFrames[0].id != 0;

    // Same white-background threshold keying as every other frame set
    for (int i = 0; i < 6; i++) {
        LoadFrame(&enemyAssets.defeatFrames[i], enemyDefeatPaths[i]); // files may not exist yet
    }
    const bool enemyDefeatOk = enemyAssets.defeatFrames[0].id != 0;

    // The enemies vector - starts empty; the wave spawner fills it (wave 1
    // spawns on the first frame) and refills it every time a wave is cleared.
    std::vector<Enemy> enemies;
    // ---------------------------------------------------------------------

    // --- Player initialization ------------------------------------------
    Player player = {};
    player.currentFrame = 0;
    player.frameTimer   = 0.0f;
    player.frameSpeed   = 0.1f;
    player.moveSpeed    = 200.0f;
    player.moving       = false;
    // facingDirection defaults to 1 (right) via the struct definition
    player.position     = { 0.0f, 0.0f };  // world origin; camera centers on it

    bool playerOk = true;
    for (int i = 0; i < 6; i++) {
        if (!LoadFrame(&player.walkFrames[i], playerFramePaths[i])) playerOk = false;
    }
    for (int i = 0; i < 6; i++) {
        LoadFrame(&player.attackFrames[i], playerAttackPaths[i]); // frame 6 optional
    }
    const bool playerAttackOk = player.attackFrames[0].id != 0;

    // Same white-background threshold keying as every other frame set
    for (int i = 0; i < 6; i++) {
        LoadFrame(&player.defeatFrames[i], playerDefeatPaths[i]);
    }
    const bool playerDefeatOk = player.defeatFrames[0].id != 0;
    // ---------------------------------------------------------------------

    // --- Follow camera: keeps the player centered on screen --------------
    Camera2D camera = {};
    camera.offset   = { GetScreenWidth() * 0.5f, GetScreenHeight() * 0.5f };
    // Y is LOCKED (2.5D): target.y == offset.y, so world Y maps 1:1 onto
    // screen Y and the horizon never bounces when the player moves up/down.
    camera.target   = { 0.0f, GetScreenHeight() * 0.5f };
    camera.rotation = 0.0f;
    camera.zoom     = 1.0f;
    // ---------------------------------------------------------------------

    // --- 2.5D layout (computed once; the window is fixed-size) -----------
    const float horizonY   = GetScreenHeight() * 0.4f;        // 40/60 split
    const float playerMinY = horizonY + PLAYER_HEIGHT * 0.5f; // feet stay on rocks
    const float rockBottom = (float)GetScreenHeight();        // visible bottom edge
    const Rectangle rockArea = { ARENA_MIN_X, horizonY,
                                 ARENA_MAX_X - ARENA_MIN_X, rockBottom - horizonY };
    int currentWave = 0;
    bool gameOver = false;     // set when the last life is lost (freezes game)
   bool gameStarted = true;  // Temporarily bypass title screen for testing
    // ---------------------------------------------------------------------

    int framesRendered = 0;
    while (!WindowShouldClose()) {
        const float dt = GetFrameTime();

        // --- Title screen: initialize defenses on ENTER -------------------
        if (!gameStarted && IsKeyPressed(KEY_ENTER)) {
            gameStarted = true;                    // defenses online
            player.hp = 100;
            player.lives = 3;
            player.position = { 0.0f, 0.0f };
            player.currentFrame = 0;
            player.frameTimer = 0.0f;
            player.isAttacking = false;
            player.attackTimer = 0.0f;
            player.damageFlashTimer = 0.0f;
            player.knockbackVelocity = 0.0f;
            player.facingDirection = 1;
            player.moving = false;
            enemies.clear();
            currentWave = 0;                       // spawner starts at wave 1
            totalKills = 0;
            weaponLevel = 1;
            upgradeNoticeTimer = 0.0f;
        }

        // --- Player update: WASD movement (gated until started) -----------
        player.moving = false;
        if (gameStarted && !gameOver) {
            Vector2 dir = { 0.0f, 0.0f };
            if (IsKeyDown(KEY_D)) dir.x += 1.0f;
            if (IsKeyDown(KEY_A)) dir.x -= 1.0f;
            if (IsKeyDown(KEY_S)) dir.y += 1.0f;
            if (IsKeyDown(KEY_W)) dir.y -= 1.0f;

            player.moving = (dir.x != 0.0f || dir.y != 0.0f);
            if (player.moving) {
                const float len = sqrtf(dir.x * dir.x + dir.y * dir.y);
                if (len > 0.0f) { dir.x /= len; dir.y /= len; }   // no diagonal speed boost
                player.position.x += dir.x * player.moveSpeed * dt;
                player.position.y += dir.y * player.moveSpeed * dt;
                if (dir.x < 0.0f) player.facingDirection = -1;
                else if (dir.x > 0.0f) player.facingDirection = 1; // keep facing on pure vertical
            }
        }

        // Knockback physics + damage-flash decay (player)
        ApplyKnockback(&player.position, &player.knockbackVelocity, dt);
        if (player.damageFlashTimer > 0.0f) player.damageFlashTimer -= dt;
        if (upgradeNoticeTimer > 0.0f) upgradeNoticeTimer -= dt;

        // Arena boundaries: feet stay on the rocks (below horizon, above bottom);
        // the clamp also contains any knockback shove.
        ClampToRocks(&player.position, playerMinY, rockBottom);

        // --- Player combat: attack on SPACE ------------------------------
        // 10 damage to every enemy within 150px in the facing half-plane.
        if (gameStarted && !gameOver && IsKeyPressed(KEY_SPACE)) {
            player.isAttacking  = true;
            player.attackTimer  = 0.0f;
            player.currentFrame = 0;
            player.frameTimer   = 0.0f;

            for (auto it = enemies.begin(); it != enemies.end(); ++it) {
                const float dx = it->position.x - player.position.x;
                const float dy = it->position.y - player.position.y;
                const float dist = sqrtf(dx * dx + dy * dy);
                const bool inFacing = (dx * (float)player.facingDirection) >= 0.0f;
                if (dist <= 150.0f && inFacing && !it->isDefeated) {
                    it->hp -= 10 + ((weaponLevel - 1) * 15);
                    it->damageFlashTimer = 0.2f;                             // red flash
                    it->knockbackVelocity = 300.0f * (float)player.facingDirection; // shoved away
                    if (it->hp <= 0) {   // killed: collapse instead of vanishing
                        it->hp = 0;
                        it->isDefeated = true;
                        it->isAttacking = false;
                        it->currentFrame = 0;
                        it->frameTimer = 0.0f;
                        it->knockbackVelocity = 0.0f;   // body stops sliding
                        it->damageFlashTimer = 0.0f;    // normal colors while falling
                        totalKills++;                   // logged as purged malware
                        weaponLevel++;                // 1-to-1 upgrade: exactly once per confirmed kill
                        upgradeNoticeTimer = 2.0f;    // refresh PAYLOAD UPGRADED (never stacks)
                    }
                }
            }
        }
        if (player.isAttacking) {
            player.attackTimer += dt;
            // Step through the 6 attack frames across the 0.4s swing
            player.frameTimer += dt;
            if (player.frameTimer >= 0.4f / 6.0f) {
                player.frameTimer = 0.0f;
                if (player.currentFrame < 5) player.currentFrame++;
            }
            if (player.attackTimer >= 0.4f) {
                player.isAttacking = false;
                player.attackTimer = 0.0f;
            }
        }
        // ---------------------------------------------------------------

        // Walk cycle: advance through the 6 individual frames on a loop
        // (frozen while swinging so the attack pose isn't stomped mid-swing)
        if (!player.isAttacking) {
            player.frameTimer += dt;
            if (player.frameTimer >= player.frameSpeed) {
                player.frameTimer = 0.0f;
                player.currentFrame = (player.currentFrame + 1) % 6;
            }
        }

        // 2.5D camera: follow horizontally only; Y stays locked so the
        // 40/60 horizon line never bounces with player movement.
        camera.target.x = player.position.x;
        camera.target.y = GetScreenHeight() * 0.5f;
        // -------------------------------------------------------------

        // --- Wave system: an empty field means the wave was cleared ------
        if (enemies.empty() && player.hp > 0) {
            currentWave++;
            const int count = currentWave * 2;                 // wave 1: 2, wave 2: 4...
            for (int i = 0; i < count; i++) {
                Enemy newEnemy = enemyAssets;                  // shares texture handles
                newEnemy.hp    = 30 + (currentWave * 10);      // tankier each wave
                newEnemy.speed = 100.0f + (currentWave * 5.0f);// faster each wave

                // Spawn just outside the camera's X view, alternating sides
                const float side = (i % 2 == 0) ? -1.0f : 1.0f;
                float spawnX = player.position.x + side * ((float)GetScreenWidth() * 0.5f + 80.0f);
                if (spawnX < ARENA_MIN_X + 40.0f) spawnX = ARENA_MAX_X - 40.0f;  // wrap to the far edge
                if (spawnX > ARENA_MAX_X - 40.0f) spawnX = ARENA_MIN_X + 40.0f;
                const float spawnY = (float)GetRandomValue((int)(horizonY + ENEMY_HEIGHT * 0.5f),
                                                           (int)(rockBottom - 10.0f));
                newEnemy.position = { spawnX, spawnY };
                enemies.push_back(newEnemy);
            }
        }
        // -------------------------------------------------------------

        // --- Enemy update: chase, attack, animate (frozen on game over) ---
        if (enemyOk && !gameOver) for (size_t ei = 0; ei < enemies.size(); ei++) {
            Enemy& enemy = enemies[ei];

            // Defeated hunters: play the collapse ONCE, lock on frame 5,
            // then rest on the ground until the despawn pass erases them.
            if (enemy.isDefeated) {
                if (enemy.currentFrame < 5) {
                    enemy.frameTimer += dt;
                    if (enemy.frameTimer >= enemy.frameSpeed) {
                        enemy.frameTimer = 0.0f;
                        enemy.currentFrame++;
                    }
                } else {
                    enemy.currentFrame = 5;   // locked - lying on the ground
                    enemy.defeatTimer += dt;
                }
                continue;                      // no movement, no attacks while falling
            }

            // Direction vector from the enemy to the player
            Vector2 toPlayer = { player.position.x - enemy.position.x,
                                 player.position.y - enemy.position.y };
            const float dist = sqrtf(toPlayer.x * toPlayer.x + toPlayer.y * toPlayer.y);

            if (!enemy.isAttacking && dist < 130.0f) {
                // In range: STOP moving and start attacking
                enemy.isAttacking  = true;
                enemy.attackTimer  = 0.0f;
                enemy.currentFrame = 0;
                enemy.frameTimer   = 0.0f;
            } else if (!enemy.isAttacking) {
                // Chase: normalized direction * speed * dt
                if (dist > 0.0001f) {
                    toPlayer.x /= dist;
                    toPlayer.y /= dist;
                    enemy.position.x += toPlayer.x * enemy.speed * dt;
                    enemy.position.y += toPlayer.y * enemy.speed * dt;
                }
            }

            // Always look at the player
            enemy.facingDirection = (player.position.x > enemy.position.x) ? 1 : -1;

            // Knockback physics + damage-flash decay (enemy)
            ApplyKnockback(&enemy.position, &enemy.knockbackVelocity, dt);
            if (enemy.damageFlashTimer > 0.0f) enemy.damageFlashTimer -= dt;

            // Arena boundaries: the enemy is trapped on the rocks as well;
            // the clamp also contains any knockback shove.
            ClampToRocks(&enemy.position, horizonY + ENEMY_HEIGHT * 0.5f, rockBottom);

            // Enemy attack: thrust frame (3) lands the hit, gated by cooldown
            if (enemy.isAttacking) {
                enemy.attackTimer += dt;
                if (enemy.currentFrame == 3 && enemy.attackCooldown <= 0.0f) {
                    player.hp -= 25;
                    player.damageFlashTimer = 0.2f;                         // red flash
                    player.knockbackVelocity = 300.0f * (float)enemy.facingDirection; // shoved away
                    enemy.attackCooldown = 1.5f;   // no instakill
                }
                if (enemy.attackTimer >= 0.4f) {
                    enemy.isAttacking = false;
                    enemy.attackTimer = 0.0f;
                    enemy.currentFrame = 0;
                }
            }
            if (enemy.attackCooldown > 0.0f) enemy.attackCooldown -= dt;

            // Cycle through the 6 frames on a loop
            enemy.frameTimer += dt;
            if (enemy.frameTimer >= enemy.frameSpeed) {
                enemy.frameTimer = 0.0f;
                enemy.currentFrame = (enemy.currentFrame + 1) % 6;
            }
        }
        // Despawn defeated hunters once their rest timer expires (> 1.5 s)
        for (auto it = enemies.begin(); it != enemies.end(); ) {
            if (it->isDefeated && it->defeatTimer > 1.5f) it = enemies.erase(it);
            else ++it;
        }

        // --- Death: immediate respawn (fast and responsive) --------------
        if (player.hp <= 0 && !gameOver) {
            if (player.lives > 0) {
                player.lives--;
                player.hp = 100;                 // fresh health bar
                player.position.x = 0.0f;        // platform center - never a corner
                player.isAttacking = false;              // cancel any attack pose
                player.knockbackVelocity = 0.0f;         // stop the fatal shove
                player.currentFrame = 0;
                player.frameTimer = 0.0f;
                enemies.clear();                 // fresh, instant start
                if (currentWave > 0) currentWave--;      // the SAME wave re-spawns
            } else {
                gameOver = true;                 // instant System Compromised
            }
        }
        if (player.hp < 0) player.hp = 0;        // clamp for HUD display

        // --- System reboot: press R on the System Compromised screen -----
        if (gameOver && IsKeyPressed(KEY_R)) {
            player.lives = 3;
            player.hp = 100;
            player.position = { 0.0f, 0.0f };    // back to the platform center
            player.isAttacking = false;
            player.attackTimer = 0.0f;
            player.currentFrame = 0;
            player.frameTimer = 0.0f;
            player.damageFlashTimer = 0.0f;
            player.knockbackVelocity = 0.0f;
            player.facingDirection = 1;
            player.moving = false;
            enemies.clear();
            currentWave = 0;   // spawner increments to 1 next frame; seeding 1
                               // would jump-straight to wave 2's size/difficulty
            weaponLevel = 1;         // fresh payload on reboot
            upgradeNoticeTimer = 0.0f;
            gameOver = false;  // (totalKills is a session-long stat: kept)
        }
        // -----------------------------------------------------------------
        BeginDrawing();
            ClearBackground(DARKGRAY);

            // Static sky backdrop: stretched over the whole window BEFORE the
            // camera, so it never moves - sky above, rocks below the horizon.
            // Static sky backdrop: fills only y = 0..horizonY (top 40% of the
            // screen), independent of the camera - it can never bounce.
            if (skyOk) {
                DrawTexturePro(sky,
                    (Rectangle){ 0, 0, (float)sky.width, (float)sky.height },
                    (Rectangle){ 0, 0, (float)GetScreenWidth(), horizonY },
                    (Vector2){ 0, 0 }, 0.0f, WHITE);
            }

            // World rendering inside the camera: player + enemy.
            // Scaled to PLAYER_HEIGHT / ENEMY_HEIGHT preserving aspect ratio.
            BeginMode2D(camera);
                // Rocky ground: starts exactly at the horizon, fills downward
                if (groundOk) DrawGroundTiled(ground, rockArea);
                // Enemies (feet anchor; attack pose while attacking)
                if (enemyOk) {
                    for (const Enemy& e : enemies) {
                        const float scale = ENEMY_HEIGHT / (float)e.walkFrames[0].height;
                        Texture2D currentTex = e.isDefeated  ? e.defeatFrames[e.currentFrame]
                                              : e.isAttacking ? e.attackFrames[e.currentFrame]
                                                               : e.walkFrames[e.currentFrame];
                        // Defeated: STRICTLY the defeat frames - never fall back
                        // to walk/attack (that fallback caused the frozen-pose bug).
                        // A blank texture draws nothing, which raylib handles safely.
                        if (!e.isDefeated && currentTex.id == 0) currentTex = e.walkFrames[e.currentFrame];
                        const float drawW = (float)currentTex.width * scale;
                        Rectangle sourceRec = { 0.0f, 0.0f,
                                                (float)currentTex.width * (float)e.facingDirection,
                                                (float)currentTex.height };
                        Rectangle destRec = { e.position.x, e.position.y, drawW, ENEMY_HEIGHT };
                        Vector2 origin = { drawW * 0.5f, ENEMY_HEIGHT };
                        const Color tint = e.damageFlashTimer > 0.0f ? RED : WHITE;
                        DrawTexturePro(currentTex, sourceRec, destRec, origin, 0.0f, tint);
                    }
                }

                // Player (feet anchor; attack pose while attacking)
                if (playerOk) {
                    const float scale = PLAYER_HEIGHT / (float)player.walkFrames[0].height;
                    Texture2D currentTex = player.isAttacking ? player.attackFrames[player.currentFrame]
                                                              : player.walkFrames[player.currentFrame];
                    if (currentTex.id == 0) currentTex = player.walkFrames[player.currentFrame];
                    const float drawW = (float)currentTex.width * scale;
                    Rectangle sourceRec = { 0.0f, 0.0f,
                                            (float)currentTex.width * (float)player.facingDirection,
                                            (float)currentTex.height };
                    Rectangle destRec = { player.position.x, player.position.y, drawW, PLAYER_HEIGHT };
                    Vector2 origin = { drawW * 0.5f, PLAYER_HEIGHT };
                    const Color tint = player.damageFlashTimer > 0.0f ? RED : WHITE;
                    DrawTexturePro(currentTex, sourceRec, destRec, origin, 0.0f, tint);
                    if (player.isAttacking) {
                        // Upgrade ring glows AROUND the swinging sprite (additive, never replaces it)
                        Vector2 center = { player.position.x, player.position.y - 60.0f };
                        float attackRadius = 30.0f + (float)weaponLevel * 3.0f;
                        if (attackRadius > 90.0f) attackRadius = 90.0f;
                        float thick = 2.0f + (float)weaponLevel * 0.5f;
                        if (thick > 8.0f) thick = 8.0f;
                        DrawCircleLines((int)center.x, (int)center.y, attackRadius, SKYBLUE);
                        DrawCircleLines((int)center.x, (int)center.y, attackRadius - thick, CYBER_CYAN);
                        DrawRing(center, attackRadius - thick, attackRadius, 0, 360, 32, Fade(CYBER_CYAN, 0.5f));
                        if (upgradeNoticeTimer > 0.0f) {
                            Vector2 sp = GetWorldToScreen2D(center, camera);
                            const char* up = "PAYLOAD UPGRADED";
                            DrawText(up, (int)sp.x - MeasureText(up, 20) / 2, (int)sp.y - 130, 20, CYBER_CYAN);
                        }
                    }
                }
            EndMode2D();

            DrawText("SMOKE TEST", 20, 20, 30, WHITE);
            DrawText(skyOk ? "[OK] Sky.jpg" : "[FAIL] Sky.jpg", 20, 60, 20, GREEN);
            DrawText(enemyOk ? "[OK] enemy walk frames x6" : "[FAIL] enemy walk frames", 20, 85, 20, GREEN);
            DrawText(playerOk ? "[OK] player walk frames x6" : "[FAIL] player walk frames", 20, 110, 20, GREEN);
            DrawText(groundOk ? "[OK] ground tile" : "[FAIL] ground tile", 20, 135, 20, GREEN);
            DrawText("Press ESC to exit", 20, GetScreenHeight() - 30, 20, RAYWHITE);

            // --- Interactive control cluster (inverted-T d-pad + attack) ---
            // Anchored where the old "WASD to move" hint used to be (20,160).
            const float btn = 46.0f;                       // 128px source scaled down
            const float dpadX = 20.0f, dpadY = 160.0f;
            struct PadBtn { float x, y, rot; int key; };
            const PadBtn pad[4] = {
                { dpadX + btn + 4.0f, dpadY,                  -90.0f, KEY_W }, // up
                { dpadX,              dpadY + btn + 4.0f,      180.0f, KEY_A }, // left
                { dpadX + btn + 4.0f, dpadY + btn + 4.0f,       90.0f, KEY_S }, // down
                { dpadX + (btn + 4.0f) * 2.0f, dpadY + btn + 4.0f, 0.0f, KEY_D }, // right
            };
            for (int i = 0; i < 4; i++) {
                Texture2D tex = IsKeyDown(pad[i].key) ? uiArrowActive : uiArrowInactive;
                if (tex.id != 0) {
                    DrawTexturePro(tex,
                        (Rectangle){ 0, 0, (float)tex.width, (float)tex.height },
                        (Rectangle){ pad[i].x + btn * 0.5f, pad[i].y + btn * 0.5f, btn, btn },
                        (Vector2){ btn * 0.5f, btn * 0.5f }, // center origin: rotates in place
                        pad[i].rot, WHITE);
                }
            }

            // Attack button (Space) right of the d-pad; pressed = darker + smaller
            if (uiAction.id != 0) {
                const bool pressed = IsKeyDown(KEY_SPACE);
                const float actSize = pressed ? 52.0f : 60.0f;
                const float actX = dpadX + (btn + 4.0f) * 3.0f + 16.0f;
                const float actY = dpadY + btn + 4.0f + (btn - actSize) * 0.5f;
                DrawTexturePro(uiAction,
                    (Rectangle){ 0, 0, (float)uiAction.width, (float)uiAction.height },
                    (Rectangle){ actX + actSize * 0.5f, actY + actSize * 0.5f, actSize, actSize },
                    (Vector2){ actSize * 0.5f, actSize * 0.5f },
                    0.0f, pressed ? (Color){ 150, 150, 150, 255 } : WHITE);
            }

            // Health bar (screen space, top-right under the wave counter) -
            // relocated from the left so the new d-pad cluster fits at (20,160)
            if (healthBarOk) {
                const int barX = GetScreenWidth() - healthBarTex.width - 20;
                const int barY = 60;
                DrawTexture(healthBarTex, barX, barY, WHITE);
                float hpRatio = (float)player.hp / 100.0f;
                if (hpRatio < 0.0f) hpRatio = 0.0f;
                if (hpRatio > 1.0f) hpRatio = 1.0f;
                DrawRectangle(barX + 6, barY + 6,
                              (int)((float)(healthBarTex.width - 12) * hpRatio),
                              healthBarTex.height - 12, GREEN);
                DrawText(TextFormat("HP: %i", player.hp),
                         barX, barY + healthBarTex.height + 4, 20, RAYWHITE);
                DrawText(TextFormat("Lives: %d", player.lives),
                         barX, barY + healthBarTex.height + 28, 20, WHITE);
            }

            // Cybersecurity HUD (top-center, terminal green) - gameplay only
            if (!gameOver) {
                const char* breachText = TextFormat("BREACH LEVEL: %i", currentWave);
                const int breachW = MeasureText(breachText, 30);
                const char* purgeText = TextFormat("ENEMIES ELIMINATED: %i", totalKills);
                const int purgeW = MeasureText(purgeText, 24);
                const char* wepText = TextFormat("WEAPON LEVEL: %i", weaponLevel);
                const int wepW = MeasureText(wepText, 26);
                const bool overclocked = weaponLevel > 1; // upgraded from initial state

                // Terminal backdrop so the green text stays readable on any sky
                const int boxW = (breachW > purgeW ? breachW : purgeW) + 40;
                const int boxH = overclocked ? 140 : 112;
                DrawRectangle((GetScreenWidth() - boxW) / 2, 10, boxW, boxH,
                              Fade(BLACK, 0.7f));

                DrawText(breachText, (GetScreenWidth() - breachW) / 2, 20, 30, LIME);
                DrawText(purgeText, (GetScreenWidth() - purgeW) / 2, 56, 24, GREEN);
                DrawText(wepText, (GetScreenWidth() - wepW) / 2, 84, 26, CYBER_CYAN);
                if (overclocked) {
                    const char* ocText = "STATUS: OVERCLOCKED";
                    DrawText(ocText, (GetScreenWidth() - MeasureText(ocText, 18)) / 2, 114, 18, CYBER_CYAN);
                }
            }

            // Title screen prompt: game is gated on ENTER, so say so on screen
            if (!gameStarted) {
                DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, 0.8f));
                const char* title = "PROTOCOL: OVERRIDE";
                DrawText(title, (GetScreenWidth() - MeasureText(title, 60)) / 2,
                         GetScreenHeight() / 2 - 90, 60, LIME);
                const char* sub = "WASD to move  |  SPACE to attack";
                DrawText(sub, (GetScreenWidth() - MeasureText(sub, 24)) / 2,
                         GetScreenHeight() / 2, 24, GREEN);
                const char* go = "PRESS [ENTER] TO INITIALIZE DEFENSES";
                DrawText(go, (GetScreenWidth() - MeasureText(go, 26)) / 2,
                         GetScreenHeight() / 2 + 50, 26, CYBER_CYAN);
            }

            // System Compromised screen (antivirus terminal theme)
            if (gameOver) {
                DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, 0.8f));
                const char* over = "SYSTEM COMPROMISED";
                DrawText(over, (GetScreenWidth() - MeasureText(over, 70)) / 2,
                         GetScreenHeight() / 2 - 90, 70, RED);
                const char* stat1 = TextFormat("Final Breach Level: %i", currentWave);
                DrawText(stat1, (GetScreenWidth() - MeasureText(stat1, 24)) / 2,
                         GetScreenHeight() / 2, 24, LIME);
                const char* stat2 = TextFormat("Total Enemies Eliminated: %i", totalKills);
                DrawText(stat2, (GetScreenWidth() - MeasureText(stat2, 24)) / 2,
                         GetScreenHeight() / 2 + 30, 24, LIME);
                DrawText("PRESS [R] TO REBOOT SYSTEM",
                         (GetScreenWidth() - MeasureText("PRESS [R] TO REBOOT SYSTEM", 24)) / 2,
                         GetScreenHeight() / 2 + 80, 24, GREEN);
                DrawText("Press ESC to quit",
                         (GetScreenWidth() - MeasureText("Press ESC to quit", 20)) / 2,
                         GetScreenHeight() / 2 + 120, 20, DARKGRAY);
            }

            framesRendered++;
            if (autoExitFrames >= 0 && framesRendered >= autoExitFrames) break;
        EndDrawing();
    }

    UnloadTexture(sky);
    UnloadTexture(ground);
    if (healthBarTex.id != 0) UnloadTexture(healthBarTex);
    if (uiArrowActive.id != 0)   UnloadTexture(uiArrowActive);
    if (uiArrowInactive.id != 0) UnloadTexture(uiArrowInactive);
    if (uiAction.id != 0)        UnloadTexture(uiAction);
    for (int i = 0; i < 6; i++) {
        // enemyAssets owns the shared GPU textures (vector copies share ids)
        if (enemyAssets.walkFrames[i].id != 0)   UnloadTexture(enemyAssets.walkFrames[i]);
        if (enemyAssets.attackFrames[i].id != 0) UnloadTexture(enemyAssets.attackFrames[i]);
        if (enemyAssets.defeatFrames[i].id != 0) UnloadTexture(enemyAssets.defeatFrames[i]);
        if (player.walkFrames[i].id != 0)        UnloadTexture(player.walkFrames[i]);
        if (player.attackFrames[i].id != 0)      UnloadTexture(player.attackFrames[i]);
        if (player.defeatFrames[i].id != 0)      UnloadTexture(player.defeatFrames[i]);
    }
    CloseWindow();

    // Exit code: 0 if core assets loaded, 2 if any failed (attack frame 6 is
    // expected to be missing, so attacks report PARTIAL without failing).
    const int result = (skyOk && groundOk && enemyOk && playerOk && healthBarOk && uiOk) ? 0 : 2;
    std::printf("Smoke test finished: sky=%s ground=%s enemy=%s player=%s bar=%s ui=%s playerAtk=%s enemyAtk=%s defeat=%s edefeat=%s frames=%d\n",
                skyOk ? "OK" : "FAIL", groundOk ? "OK" : "FAIL",
                enemyOk ? "OK" : "FAIL", playerOk ? "OK" : "FAIL",
                healthBarOk ? "OK" : "FAIL", uiOk ? "OK" : "FAIL",
                playerAttackOk ? "OK" : "PARTIAL", enemyAttackOk ? "OK" : "PARTIAL",
                playerDefeatOk ? "OK" : "MISSING", enemyDefeatOk ? "OK" : "MISSING",
                framesRendered);
    return result;
}
