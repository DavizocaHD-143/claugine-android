/* ============================================================
 * engine.h - Mini engine 2D compativel com Android 4.4+ (API 19)
 * API com os MESMOS NOMES e a MESMA LOGICA do Allegro 4.2.
 * Se voce ja programou em Allegro, e basicamente isso: BITMAP*,
 * putpixel/line/rect/circle, blit/draw_sprite, screen, key[],
 * mouse_x/mouse_y/mouse_b, install_int_ex...
 *
 * Diferencas em relacao ao Allegro real (por causa do Android):
 *   - So existe modo truecolor (32 bits). Nao tem 8bpp/paleta.
 *   - Quem controla o ciclo de vida da janela e o proprio Android,
 *     entao nao existe main()/allegro_init()/set_gfx_mode() de
 *     verdade -- no lugar disso voce implementa 4 funcoes
 *     (game_init/game_update/game_draw/game_close) que a engine
 *     chama sozinha (ver o final deste arquivo).
 * ============================================================ */
#ifndef ENGINE_H
#define ENGINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- BITMAP ---------------- */
typedef struct BITMAP {
    int w, h;
    uint32_t *dat; /* pixels truecolor, w*h, formato RGBA (byte0=R,1=G,2=B,3=A) */
} BITMAP;

/* superficie principal (equivalente ao "screen" global do Allegro) */
extern BITMAP *screen;
extern int SCREEN_W;
extern int SCREEN_H;

/* cor "chave" usada pelo draw_sprite/masked_blit pra transparencia,
 * igual ao MASK_COLOR do Allegro (rosa berrante) */
#define MASK_COLOR_R 255
#define MASK_COLOR_G 0
#define MASK_COLOR_B 255

int makecol(int r, int g, int b);
int makecol_a(int r, int g, int b, int a); /* extra: com alpha (nao existe no Allegro 4.2) */

/* ---------------- bitmaps ---------------- */
BITMAP *create_bitmap(int w, int h);
void    destroy_bitmap(BITMAP *bmp);
BITMAP *load_bitmap(const char *filename); /* le de assets/, BMP 24/32bpp sem compressao */

/* ---------------- desenho por software ---------------- */
void clear_bitmap(BITMAP *bmp);                       /* limpa pra preto */
void clear_to_color(BITMAP *bmp, int color);
void putpixel(BITMAP *bmp, int x, int y, int color);
int  getpixel(BITMAP *bmp, int x, int y);
void line(BITMAP *bmp, int x1, int y1, int x2, int y2, int color);
void rect(BITMAP *bmp, int x1, int y1, int x2, int y2, int color);
void rectfill(BITMAP *bmp, int x1, int y1, int x2, int y2, int color);
void circle(BITMAP *bmp, int x, int y, int radius, int color);
void circlefill(BITMAP *bmp, int x, int y, int radius, int color);

/* copia uma regiao de src pra dest, sem transparencia */
void blit(BITMAP *src, BITMAP *dest, int src_x, int src_y, int dest_x, int dest_y, int w, int h);

/* como blit, mas com escala (origem w,h -> destino sw,sh) */
void stretch_blit(BITMAP *src, BITMAP *dest,
                   int src_x, int src_y, int src_w, int src_h,
                   int dest_x, int dest_y, int dest_w, int dest_h);

/* como blit, mas pixels com a MASK_COLOR na origem viram transparentes */
void masked_blit(BITMAP *src, BITMAP *dest, int src_x, int src_y, int dest_x, int dest_y, int w, int h);

/* desenha um sprite inteiro em (x,y), respeitando a MASK_COLOR - o classico do Allegro */
void draw_sprite(BITMAP *bmp, BITMAP *sprite, int x, int y);

/* como draw_sprite, mas esticando pro tamanho w,h */
void stretch_sprite(BITMAP *bmp, BITMAP *sprite, int x, int y, int w, int h);

/* ---------------- teclado (bluetooth/gamepad) ---------------- */
enum {
    KEY_ESC, KEY_ENTER, KEY_SPACE,
    KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
    KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
    KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
    KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
    KEY_MAX
};

/* key[KEY_X] != 0 enquanto a tecla estiver pressionada - igual ao Allegro.
 * Util pra teclado bluetooth ou gamepad conectado no Android. */
extern int key[KEY_MAX];

int install_keyboard(void); /* stub p/ compatibilidade: so retorna 0 */

/* ---------------- mouse / toque ---------------- */
/* no touchscreen, o "mouse" e o dedo: mouse_b bit 0 = tocando a tela */
extern int mouse_x, mouse_y, mouse_b;
#define MOUSE_LEFT_BUTTON 1

int install_mouse(void); /* stub p/ compatibilidade: so retorna 0 */

/* ---------------- timer ---------------- */
/* igual ao Allegro: voce converte uma frequencia/intervalo pra "unidades de
 * timer" com essas macros e passa pra install_int_ex(). */
#define TIMERS_PER_SECOND 1193181L
#define SECS_TO_TIMER(x)  ((long)((x) * TIMERS_PER_SECOND))
#define MSEC_TO_TIMER(x)  ((long)((x) * (TIMERS_PER_SECOND / 1000)))
#define BPS_TO_TIMER(x)   ((long)(TIMERS_PER_SECOND / (x)))

int  install_timer(void); /* stub p/ compatibilidade: so retorna 0 */
void install_int_ex(void (*proc)(void), long speed); /* speed = SECS_TO_TIMER(...) etc */
void install_int(void (*proc)(void), long msec);      /* atalho: intervalo direto em ms */
void remove_int(void (*proc)(void));

void rest(int ms); /* dorme "ms" milissegundos, igual ao Allegro */

/* ---------------- log (equivalente ao allegro_message) ---------------- */
void allegro_message(const char *fmt, ...);

/* ---------------- ciclo de vida do jogo ---------------- */
/* O Android quem manda no ciclo de vida da janela, entao nao rola um
 * main() + allegro_init() + set_gfx_mode() + "while(!key[KEY_ESC])"
 * classico. No lugar disso, IMPLEMENTE estas 4 funcoes no seu jogo.c -
 * a engine chama elas sozinha, na hora certa: */
void game_init(void);       /* 1x, com "screen" ja pronto (equivale a depois do set_gfx_mode) */
void game_update(double dt); /* a cada frame, dt em segundos                                  */
void game_draw(void);        /* a cada frame, depois do game_update                           */
void game_close(void);       /* 1x, quando o app esta fechando (equivale ao allegro_exit)      */

void engine_exit(void); /* chame dentro de game_update() se quiser fechar o app */

#ifdef __cplusplus
}
#endif

#endif /* ENGINE_H */
