target("coro")
_config_project({
    project_kind = "binary"
})
on_load(function (target)
    local function rela(p)
        return path.relative(path.absolute(p, os.scriptdir()), os.projectdir())
    end
    if is_plat("windows") then
        target:add("syslinks", "Advapi32", "User32", "Gdi32","Shell32")
    end
    target:add("files", rela("./**.cpp"))
    target:add("deps", "spdlog")
end)