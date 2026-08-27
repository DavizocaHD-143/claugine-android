# mini-engine

Mini engine 2D em C, estilo Allegro 4.2, compatível com Android 4.4+ (API 19).

## Como funciona
- `engine.h` / `engine.c`: a engine em si. Framebuffer de software (`BITMAP*`)
  com `putpixel`, `linha`, `retangulo_cheio`, `circulo`, `blit`, `blit_masked`,
  carregamento de BMP, leitura de toque e timers — tudo parecido com a API
  clássica do Allegro. Por baixo, cada frame é enviado como uma textura via
  OpenGL ES 1.1 (pipeline fixa, suportada desde sempre no Android).
- `jogo.c`: seu jogo. Implemente `jogo_inicializa`, `jogo_atualiza(dt)`,
  `jogo_desenha` e `jogo_finaliza` — a engine chama essas 4 funções pra você,
  igual ao loop principal de um jogo em Allegro.
- `.github/workflows/build.yml`: compila um APK debug a cada push e deixa
  disponível como artefato do workflow (aba Actions -> última execução ->
  Artifacts).

## Rodando localmente (opcional)
Se quiser testar fora do GitHub Actions, precisa do Android Studio com NDK
25.2.9519653 e CMake 3.22.1 instalados, e rodar `./gradlew assembleDebug`
(ou `gradle assembleDebug` se não tiver o wrapper).

## Assets
Bitmaps devem ir em `app/src/main/assets/` e ser BMP de 24 ou 32 bits, sem
compressão (`engine_carrega_bmp("nome.bmp")`). Se precisar de PNG, dá pra
plugar um `stb_image.h` dentro de `engine.c` sem mexer no resto da API.

## Avisos importantes
- Este projeto foi escrito e revisado, mas **não foi compilado de fato**
  neste ambiente (aqui não tem Android NDK/SDK nem acesso à internet pra
  baixar). O primeiro build no GitHub Actions pode pedir pequenos ajustes de
  versão (ex: se a `android-actions/setup-android@v3` mudar de comportamento,
  ou se as versões de NDK/build-tools saírem do índice do SDK).
- `minSdkVersion 19` cobre o Android 4.4, mas o `compileSdkVersion`/`targetSdkVersion`
  (33) é o usado pra compilar — isso é normal e não afeta a compatibilidade
  com aparelhos antigos.
