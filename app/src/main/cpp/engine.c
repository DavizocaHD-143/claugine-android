/* ============================================================
 * engine.c - implementacao da mini engine (ver engine.h)
 *
 * Estrategia (igual ao Allegro em modo software):
 *   - "screen" e um BITMAP normal em RAM (framebuffer truecolor).
 *   - Voce desenha nele com putpixel/blit/draw_sprite/etc, igual
 *     ao Allegro.
 *   - No fim de cada frame, a engine sobe esse buffer inteiro pra
 *     uma textura OpenGL ES 1.1 e desenha um retangulo full-screen.
 *   - GLES1.1 (pipeline fixa) roda em qualquer Android >= 2.x,
 *     entao funciona liso no 4.4 (KitKat).
 * ============================================================ */
#include "engine.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include <android/log.h>
#include <android/asset_manager.h>
#include <android/keycodes.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <GLES/gl.h>

#define TAG "mini-engine"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

/* ---------------- estado global (igual as globais do Allegro) ---------------- */
BITMAP *screen = NULL;
int SCREEN_W = 0;
int SCREEN_H = 0;

int key[KEY_MAX];
int mouse_x = 0, mouse_y = 0, mouse_b = 0;

#define MAX_TIMERS 8
typedef struct {
    void (*proc)(void);
    double intervalo_seg;
    double ultima_chamada;
    int usado;
} timer_t;
static timer_t g_timers[MAX_TIMERS];

typedef struct {
    struct android_app *app;

    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;

    GLuint textura;

    int rodando;
    int deve_sair;
    int jogo_iniciado;

    struct timespec inicio;
    double t_anterior;

    int mouse_x_pendente, mouse_y_pendente, mouse_b_pendente;
} engine_estado_t;

static engine_estado_t E;

/* ---------------- cor ---------------- */

int makecol(int r, int g, int b) {
    return (int)(((uint32_t)r & 0xFF) | (((uint32_t)g & 0xFF) << 8) |
                 (((uint32_t)b & 0xFF) << 16) | (0xFFu << 24));
}

int makecol_a(int r, int g, int b, int a) {
    return (int)(((uint32_t)r & 0xFF) | (((uint32_t)g & 0xFF) << 8) |
                 (((uint32_t)b & 0xFF) << 16) | (((uint32_t)a & 0xFF) << 24));
}

/* ---------------- tempo ---------------- */

static double tempo_monotonico(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double agora = ts.tv_sec + ts.tv_nsec / 1e9;
    double inicio = E.inicio.tv_sec + E.inicio.tv_nsec / 1e9;
    return agora - inicio;
}

void rest(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

/* ---------------- timer (install_int_ex, estilo Allegro) ---------------- */

int install_timer(void) { return 0; /* stub, so existe p/ compatibilidade de codigo */ }

void install_int_ex(void (*proc)(void), long speed) {
    double intervalo_seg = (double)speed / (double)TIMERS_PER_SECOND;
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (!g_timers[i].usado) {
            g_timers[i].usado = 1;
            g_timers[i].proc = proc;
            g_timers[i].intervalo_seg = intervalo_seg;
            g_timers[i].ultima_chamada = tempo_monotonico();
            return;
        }
    }
    LOGE("install_int_ex: limite de %d timers atingido", MAX_TIMERS);
}

void install_int(void (*proc)(void), long msec) {
    install_int_ex(proc, MSEC_TO_TIMER(msec));
}

void remove_int(void (*proc)(void)) {
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (g_timers[i].usado && g_timers[i].proc == proc) {
            g_timers[i].usado = 0;
        }
    }
}

static void processa_timers(void) {
    double agora = tempo_monotonico();
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (g_timers[i].usado && agora - g_timers[i].ultima_chamada >= g_timers[i].intervalo_seg) {
            g_timers[i].ultima_chamada = agora;
            if (g_timers[i].proc) g_timers[i].proc();
        }
    }
}

/* ---------------- log ---------------- */

void allegro_message(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    LOGI("%s", buf);
}

void engine_exit(void) { E.deve_sair = 1; }

/* ---------------- teclado / mouse (stubs de instalacao, estilo Allegro) ---------------- */

int install_keyboard(void) { return 0; }
int install_mouse(void) { return 0; }

/* ---------------- bitmaps ---------------- */

BITMAP *create_bitmap(int w, int h) {
    BITMAP *b = (BITMAP *)malloc(sizeof(BITMAP));
    b->w = w;
    b->h = h;
    b->dat = (uint32_t *)calloc((size_t)w * (size_t)h, sizeof(uint32_t));
    return b;
}

void destroy_bitmap(BITMAP *bmp) {
    if (!bmp) return;
    free(bmp->dat);
    free(bmp);
}

/* leitor simples de BMP 24/32bpp sem compressao (BI_RGB).
 * Se precisar de PNG, da pra plugar stb_image.h aqui dentro depois,
 * sem mudar a assinatura de load_bitmap(). */
BITMAP *load_bitmap(const char *filename) {
    if (!E.app || !E.app->activity || !E.app->activity->assetManager) {
        LOGE("load_bitmap: asset manager indisponivel");
        return NULL;
    }
    AAssetManager *am = E.app->activity->assetManager;
    AAsset *asset = AAssetManager_open(am, filename, AASSET_MODE_BUFFER);
    if (!asset) {
        LOGE("load_bitmap: nao achei '%s' em assets/", filename);
        return NULL;
    }

    off_t tam = AAsset_getLength(asset);
    const unsigned char *buf = (const unsigned char *)AAsset_getBuffer(asset);
    if (!buf || tam < 54 || buf[0] != 'B' || buf[1] != 'M') {
        LOGE("load_bitmap: '%s' nao e um BMP valido", filename);
        AAsset_close(asset);
        return NULL;
    }

    uint32_t offset_pixels = *(uint32_t *)(buf + 10);
    int32_t largura = *(int32_t *)(buf + 18);
    int32_t altura_raw = *(int32_t *)(buf + 22);
    uint16_t bpp = *(uint16_t *)(buf + 28);
    uint32_t compressao = *(uint32_t *)(buf + 30);

    if (compressao != 0 || (bpp != 24 && bpp != 32)) {
        LOGE("load_bitmap: '%s' precisa ser BMP 24/32bpp sem compressao", filename);
        AAsset_close(asset);
        return NULL;
    }

    int altura = altura_raw < 0 ? -altura_raw : altura_raw;
    int de_cima_pra_baixo = altura_raw < 0;

    BITMAP *bmp = create_bitmap(largura, altura);
    int bpp_bytes = bpp / 8;
    int linha_bytes = ((largura * bpp_bytes + 3) / 4) * 4;

    for (int y = 0; y < altura; y++) {
        int y_arq = de_cima_pra_baixo ? y : (altura - 1 - y);
        const unsigned char *linha = buf + offset_pixels + (size_t)y_arq * linha_bytes;
        for (int x = 0; x < largura; x++) {
            const unsigned char *px = linha + x * bpp_bytes;
            unsigned char b = px[0], g = px[1], r = px[2];
            unsigned char a = (bpp_bytes == 4) ? px[3] : 0xFF;
            bmp->dat[y * largura + x] = (uint32_t)makecol_a(r, g, b, a);
        }
    }

    AAsset_close(asset);
    return bmp;
}

/* ---------------- desenho por software (mesma logica do Allegro) ---------------- */

void clear_bitmap(BITMAP *bmp) { clear_to_color(bmp, makecol(0, 0, 0)); }

void clear_to_color(BITMAP *bmp, int color) {
    int n = bmp->w * bmp->h;
    uint32_t c = (uint32_t)color;
    for (int i = 0; i < n; i++) bmp->dat[i] = c;
}

void putpixel(BITMAP *bmp, int x, int y, int color) {
    if (x < 0 || y < 0 || x >= bmp->w || y >= bmp->h) return;
    bmp->dat[y * bmp->w + x] = (uint32_t)color;
}

int getpixel(BITMAP *bmp, int x, int y) {
    if (x < 0 || y < 0 || x >= bmp->w || y >= bmp->h) return -1; /* igual ao Allegro (-1 fora da bitmap) */
    return (int)bmp->dat[y * bmp->w + x];
}

void line(BITMAP *bmp, int x1, int y1, int x2, int y2, int color) {
    int dx = abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
    int dy = -abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        putpixel(bmp, x1, y1, color);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

void rect(BITMAP *bmp, int x1, int y1, int x2, int y2, int color) {
    line(bmp, x1, y1, x2, y1, color);
    line(bmp, x1, y2, x2, y2, color);
    line(bmp, x1, y1, x1, y2, color);
    line(bmp, x2, y1, x2, y2, color);
}

void rectfill(BITMAP *bmp, int x1, int y1, int x2, int y2, int color) {
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= bmp->w) x2 = bmp->w - 1;
    if (y2 >= bmp->h) y2 = bmp->h - 1;
    uint32_t c = (uint32_t)color;
    for (int y = y1; y <= y2; y++) {
        uint32_t *linha = bmp->dat + (size_t)y * bmp->w;
        for (int x = x1; x <= x2; x++) linha[x] = c;
    }
}

void circle(BITMAP *bmp, int x, int y, int radius, int color) {
    int cx = x, cy = y, r = radius;
    int px = r, py = 0, err = 0;
    while (px >= py) {
        putpixel(bmp, cx + px, cy + py, color);
        putpixel(bmp, cx + py, cy + px, color);
        putpixel(bmp, cx - py, cy + px, color);
        putpixel(bmp, cx - px, cy + py, color);
        putpixel(bmp, cx - px, cy - py, color);
        putpixel(bmp, cx - py, cy - px, color);
        putpixel(bmp, cx + py, cy - px, color);
        putpixel(bmp, cx + px, cy - py, color);
        py++;
        if (err <= 0) err += 2 * py + 1;
        if (err > 0) { px--; err -= 2 * px + 1; }
    }
}

void circlefill(BITMAP *bmp, int x, int y, int radius, int color) {
    for (int yy = -radius; yy <= radius; yy++) {
        int dx = (int)(sqrtf((float)(radius * radius - yy * yy)));
        line(bmp, x - dx, y + yy, x + dx, y + yy, color);
    }
}

static void blit_interno(BITMAP *src, BITMAP *dest,
                          int sx, int sy, int sw, int sh,
                          int dx, int dy, int dw, int dh,
                          int usa_mascara) {
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    uint32_t mascara = (uint32_t)makecol(MASK_COLOR_R, MASK_COLOR_G, MASK_COLOR_B);
    for (int y = 0; y < dh; y++) {
        int oy = dy + y;
        if (oy < 0 || oy >= dest->h) continue;
        int sy_amostra = sy + (y * sh) / dh;
        for (int x = 0; x < dw; x++) {
            int ox = dx + x;
            if (ox < 0 || ox >= dest->w) continue;
            int sx_amostra = sx + (x * sw) / dw;
            if (sx_amostra < 0 || sx_amostra >= src->w || sy_amostra < 0 || sy_amostra >= src->h) continue;
            uint32_t cor = src->dat[sy_amostra * src->w + sx_amostra];
            if (usa_mascara && (cor & 0x00FFFFFFu) == (mascara & 0x00FFFFFFu)) continue;
            dest->dat[oy * dest->w + ox] = cor;
        }
    }
}

void blit(BITMAP *src, BITMAP *dest, int src_x, int src_y, int dest_x, int dest_y, int w, int h) {
    blit_interno(src, dest, src_x, src_y, w, h, dest_x, dest_y, w, h, 0);
}

void stretch_blit(BITMAP *src, BITMAP *dest,
                   int src_x, int src_y, int src_w, int src_h,
                   int dest_x, int dest_y, int dest_w, int dest_h) {
    blit_interno(src, dest, src_x, src_y, src_w, src_h, dest_x, dest_y, dest_w, dest_h, 0);
}

void masked_blit(BITMAP *src, BITMAP *dest, int src_x, int src_y, int dest_x, int dest_y, int w, int h) {
    blit_interno(src, dest, src_x, src_y, w, h, dest_x, dest_y, w, h, 1);
}

void draw_sprite(BITMAP *bmp, BITMAP *sprite, int x, int y) {
    blit_interno(sprite, bmp, 0, 0, sprite->w, sprite->h, x, y, sprite->w, sprite->h, 1);
}

void stretch_sprite(BITMAP *bmp, BITMAP *sprite, int x, int y, int w, int h) {
    blit_interno(sprite, bmp, 0, 0, sprite->w, sprite->h, x, y, w, h, 1);
}

/* ---------------- entrada: teclado e toque/mouse ---------------- */

static int android_key_para_KEY(int32_t keycode) {
    switch (keycode) {
        case AKEYCODE_BACK: return KEY_ESC;
        case AKEYCODE_ENTER: return KEY_ENTER;
        case AKEYCODE_SPACE: return KEY_SPACE;
        case AKEYCODE_DPAD_UP: return KEY_UP;
        case AKEYCODE_DPAD_DOWN: return KEY_DOWN;
        case AKEYCODE_DPAD_LEFT: return KEY_LEFT;
        case AKEYCODE_DPAD_RIGHT: return KEY_RIGHT;
        default:
            if (keycode >= AKEYCODE_0 && keycode <= AKEYCODE_9)
                return KEY_0 + (keycode - AKEYCODE_0);
            if (keycode >= AKEYCODE_A && keycode <= AKEYCODE_Z)
                return KEY_A + (keycode - AKEYCODE_A);
            return -1;
    }
}

static int32_t engine_cuida_input(struct android_app *app, AInputEvent *event) {
    int32_t tipo = AInputEvent_getType(event);

    if (tipo == AINPUT_EVENT_TYPE_MOTION) {
        int32_t acao = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        E.mouse_x_pendente = (int)AMotionEvent_getX(event, 0);
        E.mouse_y_pendente = (int)AMotionEvent_getY(event, 0);
        if (acao == AMOTION_EVENT_ACTION_DOWN || acao == AMOTION_EVENT_ACTION_MOVE) {
            E.mouse_b_pendente = MOUSE_LEFT_BUTTON;
        } else if (acao == AMOTION_EVENT_ACTION_UP || acao == AMOTION_EVENT_ACTION_CANCEL) {
            E.mouse_b_pendente = 0;
        }
        return 1;
    }

    if (tipo == AINPUT_EVENT_TYPE_KEY) {
        int32_t keycode = AKeyEvent_getKeyCode(event);
        int32_t acao = AKeyEvent_getAction(event);
        int idx = android_key_para_KEY(keycode);
        if (idx >= 0 && idx < KEY_MAX) {
            key[idx] = (acao == AKEY_EVENT_ACTION_DOWN) ? 1 : 0;
            /* consome o botao "voltar" (KEY_ESC) pra ele nao fechar o app sozinho;
             * seu jogo decide o que fazer checando key[KEY_ESC] */
            if (keycode == AKEYCODE_BACK) return 1;
        }
        return 0;
    }

    return 0;
}

static void atualiza_entrada_por_frame(void) {
    mouse_x = E.mouse_x_pendente;
    mouse_y = E.mouse_y_pendente;
    mouse_b = E.mouse_b_pendente;
}

/* ---------------- EGL / GLES1 ---------------- */

static int inicializa_display(void) {
    const EGLint atributos[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_BLUE_SIZE, 5,
        EGL_GREEN_SIZE, 6,
        EGL_RED_SIZE, 5,
        EGL_NONE
    };
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(display, NULL, NULL);

    EGLConfig config;
    EGLint num_config;
    eglChooseConfig(display, atributos, &config, 1, &num_config);

    EGLint formato;
    eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &formato);
    ANativeWindow_setBuffersGeometry(E.app->window, 0, 0, formato);

    EGLSurface surface = eglCreateWindowSurface(display, config, E.app->window, NULL);
    EGLContext context = eglCreateContext(display, config, NULL, NULL); /* sem CLIENT_VERSION = GLES1 */

    if (eglMakeCurrent(display, surface, surface, context) == EGL_FALSE) {
        LOGE("eglMakeCurrent falhou");
        return -1;
    }

    int w, h;
    eglQuerySurface(display, surface, EGL_WIDTH, &w);
    eglQuerySurface(display, surface, EGL_HEIGHT, &h);

    E.display = display;
    E.surface = surface;
    E.context = context;

    SCREEN_W = w;
    SCREEN_H = h;
    screen = create_bitmap(w, h);

    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrthof(0, (GLfloat)w, (GLfloat)h, 0, -1, 1); /* origem no canto sup. esquerdo, igual Allegro */
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_DITHER);
    glEnable(GL_TEXTURE_2D);

    glGenTextures(1, &E.textura);
    glBindTexture(GL_TEXTURE_2D, E.textura);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    return 0;
}

static void destroi_display(void) {
    if (E.display != EGL_NO_DISPLAY) {
        eglMakeCurrent(E.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (E.context != EGL_NO_CONTEXT) eglDestroyContext(E.display, E.context);
        if (E.surface != EGL_NO_SURFACE) eglDestroySurface(E.display, E.surface);
        eglTerminate(E.display);
    }
    E.display = EGL_NO_DISPLAY;
    E.context = EGL_NO_CONTEXT;
    E.surface = EGL_NO_SURFACE;
    destroy_bitmap(screen);
    screen = NULL;
}

/* sobe "screen" pra textura e desenha um quad full-screen (equivale ao vsync/flip) */
static void apresenta_frame(void) {
    if (E.display == EGL_NO_DISPLAY) return;

    glBindTexture(GL_TEXTURE_2D, E.textura);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SCREEN_W, SCREEN_H, GL_RGBA, GL_UNSIGNED_BYTE, screen->dat);

    GLfloat verts[] = {
        0, 0,
        (GLfloat)SCREEN_W, 0,
        0, (GLfloat)SCREEN_H,
        (GLfloat)SCREEN_W, (GLfloat)SCREEN_H,
    };
    GLfloat texcoords[] = { 0, 0, 1, 0, 0, 1, 1, 1 };

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, verts);
    glTexCoordPointer(2, GL_FLOAT, 0, texcoords);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);

    eglSwapBuffers(E.display, E.surface);
}

/* ---------------- comandos do ciclo de vida do Android ---------------- */

static void engine_cuida_comando(struct android_app *app, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (E.app->window != NULL) {
                if (inicializa_display() == 0) {
                    E.rodando = 1;
                    if (!E.jogo_iniciado) {
                        clock_gettime(CLOCK_MONOTONIC, &E.inicio);
                        E.t_anterior = tempo_monotonico();
                        game_init();
                        E.jogo_iniciado = 1;
                    }
                }
            }
            break;
        case APP_CMD_TERM_WINDOW:
            E.rodando = 0;
            destroi_display();
            break;
        case APP_CMD_DESTROY:
            if (E.jogo_iniciado) game_close();
            break;
        default:
            break;
    }
}

void android_main(struct android_app *app) {
    memset(&E, 0, sizeof(E));
    memset(key, 0, sizeof(key));
    E.app = app;
    E.display = EGL_NO_DISPLAY;
    app->userData = &E;
    app->onAppCmd = engine_cuida_comando;
    app->onInputEvent = engine_cuida_input;

    while (1) {
        int eventos;
        struct android_poll_source *fonte;

        int timeout_ms = E.rodando ? 0 : -1; /* -1 = bloqueia ate ter evento (app em pausa) */
        while (ALooper_pollAll(timeout_ms, NULL, &eventos, (void **)&fonte) >= 0) {
            if (fonte != NULL) fonte->process(app, fonte);
            if (app->destroyRequested != 0) return;
            timeout_ms = E.rodando ? 0 : -1;
        }

        if (E.rodando && E.jogo_iniciado) {
            double agora = tempo_monotonico();
            double dt = agora - E.t_anterior;
            E.t_anterior = agora;

            atualiza_entrada_por_frame();
            processa_timers();
            game_update(dt);
            game_draw();
            apresenta_frame();

            if (E.deve_sair) {
                ANativeActivity_finish(app->activity);
                E.deve_sair = 0;
            }
        }
    }
}
