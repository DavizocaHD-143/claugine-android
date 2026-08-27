/* ============================================================
 * jogo.c - exemplo de uso, no estilo classico do Allegro
 * Bolinha quicando na tela; tocar (ou usar o dedo) muda a cor.
 * ============================================================ */
#include "engine.h"
#include <stdlib.h>

static float bola_x, bola_y;
static float vel_x = 220.0f, vel_y = 180.0f;
static const int RAIO = 24;
static int cor_bola;

static void escolhe_cor_aleatoria(void) {
    cor_bola = makecol(rand() % 256, rand() % 256, rand() % 256);
}

void game_init(void) {
    bola_x = SCREEN_W / 2.0f;
    bola_y = SCREEN_H / 2.0f;
    escolhe_cor_aleatoria();
    allegro_message("jogo iniciado: tela %dx%d", SCREEN_W, SCREEN_H);
}

void game_update(double dt) {
    bola_x += vel_x * (float)dt;
    bola_y += vel_y * (float)dt;

    if (bola_x - RAIO < 0)        { bola_x = RAIO;          vel_x = -vel_x; escolhe_cor_aleatoria(); }
    if (bola_x + RAIO > SCREEN_W) { bola_x = SCREEN_W - RAIO; vel_x = -vel_x; escolhe_cor_aleatoria(); }
    if (bola_y - RAIO < 0)        { bola_y = RAIO;          vel_y = -vel_y; escolhe_cor_aleatoria(); }
    if (bola_y + RAIO > SCREEN_H) { bola_y = SCREEN_H - RAIO; vel_y = -vel_y; escolhe_cor_aleatoria(); }

    /* toque na tela = mouse, igual ao Allegro (mouse_b & 1 -> botao esquerdo) */
    if (mouse_b & MOUSE_LEFT_BUTTON) {
        bola_x = (float)mouse_x;
        bola_y = (float)mouse_y;
    }

    /* teclado bluetooth/gamepad, se tiver: seta pra mudar a cor */
    if (key[KEY_SPACE]) escolhe_cor_aleatoria();
}

void game_draw(void) {
    clear_to_color(screen, makecol(20, 20, 30));
    circlefill(screen, (int)bola_x, (int)bola_y, RAIO, cor_bola);
    circle(screen, (int)bola_x, (int)bola_y, RAIO, makecol(255, 255, 255));
}

void game_close(void) {
    allegro_message("jogo encerrado");
}
