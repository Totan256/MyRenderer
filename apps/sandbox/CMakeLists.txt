# apps/sandbox/CMakeLists.txt


# apps/sandbox/CMakeLists.txt

# ⭕ ソースファイル（.cpp）は add_executable に書く
add_executable(SandboxApp
    main.cpp
    Renderer.cpp
)

# ⭕ リンクするライブラリ（Coreなど）は target_link_libraries に書く
target_link_libraries(SandboxApp PRIVATE 
    RendererCore
)