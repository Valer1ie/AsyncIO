rule("basic_settings")

on_load(function (target)
    local optional_get = function (key)
        local value = target:get(key)
        if value == nil then
            value = target:extraconf(key)
        end
        return value
    end
    -- local proj_kind = optional_get("project_kind", "phony")
    -- target:set("kind", proj_kind)
    local c_standard = target:values("c_standard")
    local cxx_standard = target:values("cxx_standard")
    if type(c_standard) == "string" and  type(cxx_standard) == "string" then
        target:set("languages", c_standard, cxx_standard)
    else
        target:set("languages", "clatest", "c++20")
    end

    if is_mode("debug") then
        target:set("runtimes", "MDd")
        target:set("optimize", "none")
        target:set("warnings", "none")
        target:add("cxflags", "/GS", "/Gd", {
            tools = {"clang_cl", "cl"}
        })
        target:add("cxflags", "/Zc:preprocessor", {
            tools = "cl"
        });
    else
        target:set("runtimes", "MD")
        target:set("optimize", "aggressive")
        target:set("warnings", "none")
        target:add("cxflags", "/GS-", "/Gd", {
            tools = {"clang_cl", "cl"}
        })
        target:add("cxflags", "/Zc:preprocessor", {
            tools = "cl"
        })
    end

end)

rule_end()
if _config_rules == nil then
    _config_rules = {"basic_settings"}
end
if not _config_project then
    function _config_project(config)
        if type(config) == "table" then
            for k, v in pairs(config) do
                set_values(k, v)
            end
        end
        if type(_config_rules) == "table" then
            add_rules(_config_rules)
        end
    end
end