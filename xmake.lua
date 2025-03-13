set_project("icue-battery")
set_languages("cxx20")

add_rules("mode.debug")
add_rules("mode.release")

add_toolchains("msvc")

target("icue-battery")
    set_default(true)
    set_kind("binary")
    add_files("src/main.cpp")
    set_symbols("debug")
    add_links("Shell32", "User32")

    -- iCUE SDK
    add_headerfiles("deps/cue-sdk/include/iCUESDK/**")
    add_includedirs("deps/cue-sdk/include/iCUESDK")
    add_links("deps/cue-sdk/lib/x64/iCUESDK.x64_2019")
    set_configdir("$(buildir)/$(plat)/$(arch)/$(mode)")
    add_configfiles("deps/cue-sdk/redist/x64/iCUESDK.x64_2019.dll", { onlycopy = true })
