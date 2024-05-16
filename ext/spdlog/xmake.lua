includes("build_proj.lua")
target("spdlog")
set_kind("static")
add_includedirs("include", {public = true})
add_rules("basic_settings")
on_load(function(target)
	local function rela(p)
        return path.relative(path.absolute(p, os.scriptdir()), os.projectdir())
    end
    if get_config("spdlog_only_fmt") then
        target:add("defines", "FMT_CONSTEVAL=constexpr", "FMT_USE_CONSTEXPR=1", "FMT_EXCEPTIONS=0", {
            public = true
        })
        target:add("headerfiles", rela("include/spdlog/fmt/**.h"))
        target:add("files", rela("src/bundled_fmtlib_format.cpp"))
    else
        target:add("defines", "SPDLOG_NO_EXCEPTIONS", "SPDLOG_NO_THREAD_ID",
            "FMT_CONSTEVAL=constexpr", "FMT_USE_CONSTEXPR=1", "FMT_EXCEPTIONS=0", {
                public = true
            })
        target:add("headerfiles", rela("include/**.h"))
        target:add("files", rela("src/*.cpp"))
    end
    target:add("defines", "SPDLOG_COMPILED_LIB", {
        public = true
    })
	if is_plat("windows") then
		target:add("defines","_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING", {
			public = true})
	end
end)
target_end()
