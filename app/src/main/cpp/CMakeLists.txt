cmake_minimum_required(VERSION 3.22.1)
project(miniengine C)

set(CMAKE_C_STANDARD 11)

# fonte do android_native_app_glue que vem dentro do próprio NDK
set(APP_GLUE_DIR ${ANDROID_NDK}/sources/android/native_app_glue)
add_library(native_app_glue STATIC ${APP_GLUE_DIR}/android_native_app_glue.c)
include_directories(${APP_GLUE_DIR})

add_library(jogo SHARED
    engine.c
    jogo.c
)

target_link_libraries(jogo
    native_app_glue
    android
    log
    EGL
    GLESv1_CM
    m
)

# necessário pro android_native_app_glue: garante que o linker não descarte
# o ponto de entrada ANativeActivity_onCreate
set_target_properties(jogo PROPERTIES LINK_FLAGS "-u ANativeActivity_onCreate")
