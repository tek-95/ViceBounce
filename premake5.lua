local sdk = os.getenv("PLUGIN_SDK_DIR")
assert(sdk, "PLUGIN_SDK_DIR must be set")
workspace "SABounce"
    location "build"
    configurations { "Release" }
    architecture "x86"
    system "windows"
    systemversion "latest"
project "SABounce"
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
    files { "src/**.cpp", "src/**.h" }
    defines { "GTASA", "PLUGIN_SGV_10US", "RW", "_CRT_SECURE_NO_WARNINGS", "_DX9_SDK_INSTALLED" }
    includedirs { sdk .. "/plugin_sa", sdk .. "/plugin_sa/game_sa", sdk .. "/plugin_sa/game_sa/enums", sdk .. "/plugin_sa/game_sa/rw", sdk .. "/shared", sdk .. "/shared/game", sdk .. "/safetyhook", sdk .. "/shared/dxsdk" }
    libdirs { sdk .. "/output/lib", sdk .. "/shared/dxsdk" }
    links { "Plugin", "d3d9", "d3dx9", "user32", "winmm" }

project "SABounceTests"
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
