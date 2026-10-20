local sdk = os.getenv("PLUGIN_SDK_DIR")
assert(sdk, "PLUGIN_SDK_DIR must be set")
workspace "ViceBounce"
    location "build"
    configurations { "Release" }
    architecture "x86"
    system "windows"
    systemversion "latest"
project "ViceBounce"
    kind "SharedLib"
    language "C++"
    cppdialect "C++latest"
    toolset "v143"
    staticruntime "On"
    runtime "Release"
    optimize "On"
    characterset "MBCS"
    targetextension ".asi"
    targetdir "bin"
    objdir "build/obj"
    files { "src/**.cpp" }
    defines { "GTAVC", "PLUGIN_SGV_STEAM", "PLUGIN_SGV_10EN", "RW", "_CRT_SECURE_NO_WARNINGS", "_DX9_SDK_INSTALLED" }
    includedirs { sdk .. "/plugin_vc", sdk .. "/plugin_vc/game_vc", sdk .. "/plugin_vc/game_vc/enums", sdk .. "/plugin_vc/game_vc/rw", sdk .. "/shared", sdk .. "/shared/game", sdk .. "/safetyhook", sdk .. "/shared/dxsdk" }
    libdirs { sdk .. "/output/lib", sdk .. "/shared/dxsdk" }
    links { "Plugin_VC", "d3d9", "d3dx9", "user32", "winmm" }


project "ViceBounceTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++17"
    toolset "v143"
    staticruntime "On"
    runtime "Release"
    optimize "On"
    files { "tests/Tests.cpp" }
    targetdir "bin"
    objdir "build/test-obj"

project "ViceBounceMusicTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++17"
    toolset "v143"
    staticruntime "On"
    runtime "Release"
    optimize "On"
    files { "tests/MusicTests.cpp" }
    links { "winmm" }
    targetdir "bin"
    objdir "build/music-test-obj"

project "ViceBounceImporterHostTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++17"
    toolset "v143"
    staticruntime "On"
    runtime "Release"
    files { "tests/ImporterHostTests.cpp" }
    targetdir "bin"
    objdir "build/importer-test-obj"
