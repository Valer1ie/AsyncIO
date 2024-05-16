set_xmakever("2.8.1")
add_rules("mode.debug", "mode.release")
set_policy("build.ccache", false)
includes( "scripts/xmake_configs.lua")
includes("ext/spdlog","src")

if is_arch("x64", "x86_64", "amd64") then
    if is_mode("debug") then 
    set_targetdir("bin/debug")
    else 
    set_targetdir("bin/release")
    end
end 