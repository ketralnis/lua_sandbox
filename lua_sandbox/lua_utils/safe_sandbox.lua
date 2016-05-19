-- save a pointer to globals that would be unreachable in sandbox
local e=_ENV

function make_sandbox()
    -- sample sandbox environment
    sandbox_env = {
        error = error,
        ipairs = ipairs,
        next = next,
        pairs = pairs,
        pcall = pcall,
        tonumber = tonumber,
        tostring = tostring,
        type = type,
        unpack = unpack,
        coroutine = {
            create = coroutine.create, resume = coroutine.resume,
            running = coroutine.running, status = coroutine.status,
            wrap = coroutine.wrap, yield = coroutine.yield,
        },
        string = {
            byte = string.byte, char = string.char, format = string.format,
            len = string.len, lower = string.lower, rep = string.rep,
            reverse = string.reverse, upper = string.upper,

            -- note!
            -- we include these in our example sandbox, but depending on how
            -- much you trust your lua code you may want to disallow them. they
            -- can be used to hang the interpreter because it's possible to
            -- create pathological patterns like:
            --     string.find(("a"):rep(1e4), ".-.-.-.-b$")
            -- and the time-limiting debug hooks can't execute while they are
            -- running
            find = string.find, gmatch = string.gmatch, gsub = string.gsub,
            match = string.match, sub = string.sub,
        },
        table = {
            insert = table.insert, maxn = table.maxn, remove = table.remove,
            sort = table.sort, pack = table.pack, unpack = table.unpack,
        },
        math = {
            abs = math.abs, acos = math.acos, asin = math.asin,
            atan = math.atan, atan2 = math.atan2, ceil = math.ceil, cos = math.cos,
            cosh = math.cosh, deg = math.deg, exp = math.exp, floor = math.floor,
            fmod = math.fmod, frexp = math.frexp, huge = math.huge,
            ldexp = math.ldexp, log = math.log, log10 = math.log10, max = math.max,
            min = math.min, modf = math.modf, pi = math.pi, pow = math.pow,
            rad = math.rad, random = math.random, sin = math.sin, sinh = math.sinh,
            sqrt = math.sqrt, tan = math.tan, tanh = math.tanh
        },
        os = {
            clock = os.clock, difftime = os.difftime, time = os.time
        },
    }

    return sandbox_env
end

function run_sandbox(env_globals, code, desc)
    local sandbox_env = {}

    for k,v in pairs(make_sandbox()) do
        sandbox_env[k] = v
    end

    for k,v in pairs(env_globals) do
        sandbox_env[k] = v
    end

    local ret = {}

    for library, segment in ipairs(code) do
        local segment_desc = desc.."#"..library
        local fn = load(segment, segment_desc, "t", sandbox_env)
        if not fn then
            error("couldn't load segment " .. segment_desc)
        end
        ret = table.pack(fn())
    end

    return table.unpack(ret)
end

return run_sandbox(env or {}, code, desc or 'code')
