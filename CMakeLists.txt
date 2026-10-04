cmake_minimum_required(VERSION 3.16)
project(RaylibApp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(raylib)

# The game itself is RaylibGame.dll; RaylibApp.exe is a small launcher for it.
add_library(RaylibGame SHARED
    src/main.cpp src/Chunk.cpp src/World.cpp src/Player.cpp src/SoundSystem.cpp src/BreakOverlay.cpp src/Hud.cpp src/Sky.cpp
    src/Hand.cpp src/Models.cpp src/Inventory.cpp src/GuiFont.cpp src/ItemIcons.cpp src/DroppedItems.cpp src/PlayerModel.cpp
    src/InventoryScreen.cpp src/BlockParticles.cpp src/MenuScreen.cpp src/Saves.cpp src/WorldScreens.cpp
    src/BlockBase.cpp src/BlocksBasic.cpp src/BlocksPlants.cpp src/BlocksFluids.cpp src/BlocksRedstone.cpp src/BlocksPiston.cpp
    src/BlocksBuilding.cpp src/BlocksContainers.cpp src/TileEntity.cpp src/TreeGen.cpp src/Items.cpp src/Crafting.cpp
    src/BlockRender.cpp src/Entities.cpp src/TextureFX.cpp src/TileEntityRenderer.cpp src/SignEditScreen.cpp
    src/Options.cpp src/OptionsScreen.cpp src/Gui.cpp src/BlocksModern.cpp)
target_link_libraries(RaylibGame PRIVATE raylib)

# raylib already compiles stb_perlin.h into raylib.lib (used internally for
# GenImagePerlinNoise); reuse it for terrain noise instead of vendoring our own copy.
target_include_directories(RaylibGame PRIVATE ${raylib_SOURCE_DIR}/src/external)

if(WIN32)
    target_link_libraries(RaylibGame PRIVATE winmm)
endif()

add_executable(${PROJECT_NAME} src/Launcher.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE RaylibGame)

set_target_properties(${PROJECT_NAME} PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})

# Rebuilds resources/textures from Minecraft 1.21's assets; not part of the game build.
#   cmake --build build --config Release --target ModernTextures
#   build/Release/ModernTextures.exe <.minecraft/versions/1.21/1.21/assets/minecraft> resources
add_executable(ModernTextures EXCLUDE_FROM_ALL tools/ModernTextures.cpp)
target_link_libraries(ModernTextures PRIVATE raylib)
target_include_directories(ModernTextures PRIVATE src)

# "cmake --install build --config Release --component Game" (as administrator) puts the game in C:/Program Files/RaylibApp:
# RaylibApp.exe, RaylibGame.dll and resources/.
install(TARGETS ${PROJECT_NAME} RaylibGame RUNTIME DESTINATION . COMPONENT Game)
install(DIRECTORY resources DESTINATION . COMPONENT Game PATTERN ".gitkeep" EXCLUDE)
