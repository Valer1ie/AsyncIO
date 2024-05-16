if not _config_project then
	function _config_project(config)
		if type(_configs) == "table" then
			for k, v in pairs(_configs) do
				set_values(k, v)
			end
		end
		if type(_config_rules) == "table" then
			Print("add %s rules", _config_rules)
			add_rules(_config_rules)
		end
		if type(config) == "table" then
			for k, v in pairs(config) do
				set_values(k, v)
			end
		end
	end
end
