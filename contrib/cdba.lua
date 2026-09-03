-- SPDX-License-Identifier: BSD-3-Clause
-- SPDX-FileCopyrightText: 2026 Qualcomm Technologies, Inc. and/or its subsidiaries

local yaml = require("lyaml")
local stat = require("posix.sys.stat")
local unistd = require("posix.unistd")
local pwd = require("posix.pwd")

local function read_cdba_cfg()
    local pw = pwd.getpwnam("cdba")
    if not pw then
        return nil
    end

    local fd = io.open(pw.pw_dir .. "/.cdba", "r")
    if not fd then
        fd = io.open("/etc/cdba", "r")
	if not fd then
            return nil
	end
    end

    local data = fd:read("*a")
    fd:close()

    return yaml.load(data)
end

local cdba_cfg = read_cdba_cfg()

local function file_exists(path)
    local fd = io.open(path, "r")
    if fd then
        fd:close()
        return true
    end

    return false
end

local function stat_file(path)
    local st, err = stat.stat(path)
    if not st then
        return nil, ("stat failed for '%s': %s"):format(path, err)
    end

    return st
end

local function dev_major(dev)
    local hi = math.floor(dev / 2 ^ 32)

    return (math.floor(dev / 2 ^ 8) % 2 ^ 12) + (hi - hi % 2 ^ 12)
end

local function dev_minor(dev)
    local mid = math.floor(dev / 2 ^ 12)

    return (dev % 2 ^ 8) + (mid - mid % 2 ^ 8)
end

local function parse_proc_locks()
    local fd, err = io.open("/proc/locks", "r")
    if not fd then
        return nil, err
    end

    local records = {}

    for line in fd:lines() do
        local id, kind, mode, ltype, pid, maj, min, ino, first, last = line:match(
            "^(%d+):%s+(%S+)%s+(%S+)%s+(%S+)%s+(%-?%d+)%s+(%x+):(%x+):(%d+)%s+(%d+)%s+(%S+)$"
        )

        if id then
            records[#records + 1] = {
                id = tonumber(id),
                kind = kind,
                mode = mode,
                type = ltype,
                pid = tonumber(pid),
                major = tonumber(maj, 16),
                minor = tonumber(min, 16),
                inode = tonumber(ino),
                start = tonumber(first),
                ["end"] = last,
            }
        end
    end

    fd:close()
    return records
end

local function match_lock(locks, st)
    local dev = st.st_dev
    local inode = st.st_ino

    local major = dev_major(dev)
    local minor = dev_minor(dev)

    for _, lock in ipairs(locks) do
        if lock.major == major and lock.minor == minor and lock.inode == inode then
            return lock
         end
    end

    return nil
end

local function uptime()
    local fd, err = io.open("/proc/uptime", "r")
    if not fd then
        return nil, err
    end

    local line = fd:read("*l")
    fd:close()

    local up = line and line:match("^(%S+)")
    if not up then
        return nil, "unexpected /proc/uptime format"
    end

    return tonumber(up)
end

local function proc_starttime(pid)
    local fd, err = io.open("/proc/" .. pid .. "/stat", "r")
    if not fd then
        return nil, err
    end

    local line = fd:read("*l")
    fd:close()
    if not line then
        return nil, "empty proc stat entry"
    end

    local after_comm = line:match(".*%)%s*(.*)$")
    if not after_comm then
        return nil, "unexpected proc stat format"
    end

    local fields = {}
    for tok in after_comm:gmatch("%S+") do
        fields[#fields + 1] = tok
    end

    local starttime = fields[22 - 2]
    if not starttime then
        return nil, "starttime missing in proc stat entry"
    end

    return tonumber(starttime)
end

local function sec_since_started(lock)
    local ticks, err = proc_starttime(lock.pid)
    if not ticks then
        return nil, err
    end

    local up, err = uptime()
    if not up then
        return nil, err
    end

    return math.floor(up - (ticks / unistd.sysconf(unistd._SC_CLK_TCK)))
end

local function cdba_dev_status(dev)
    local locks, err = parse_proc_locks()
    if not locks then
        return "ERROR: " .. err
    end

    local path = "/tmp/cdba-" .. dev.board .. ".lock"
    local state = "idle"

    if file_exists(path) then
        local st, err = stat_file(path)
        if not st then
            return "ERROR: " .. err
        end

        local lock = match_lock(locks, st)
        if lock then
            local sec, err = sec_since_started(lock)
            if not sec then
                return "ERROR: " .. err
            end

            state = string.format("running (%ds)", sec)
        end
    end

    return string.format("%-20s%s", dev.name .. ":", state)
end

--- Conky-facing entry point for rendering cdba device info.
--
-- Called as ${lua cdba <index> <field>} from conky.config, once per field per
-- device, with index counting up from 1. Returns "" if index exceeds the
-- number of devices configured in ~/.cdba, which conky templates use as the
-- sentinel to stop rendering further rows.
--
-- @param index 1-based device index, as a string or number
-- @param field one of "name", "board", or "status"
-- @return the requested field value or "" after the last device, or an
-- "ERROR: ..." string on invalid input
function conky_cdba(index, field)
    local idx = tonumber(index)

    if not cdba_cfg then
        return "ERROR: failed to load the cdba config"
    end

    if not idx or idx < 1 then
        return "ERROR: invalid index value: " .. tostring(index)
    end

    if idx > #cdba_cfg.devices then
        return ""
    end

    local dev = cdba_cfg.devices[idx]

    if field == "name" then
        return dev.name
    elseif field == "board" then
        return dev.board
    elseif field == "status" then
        return cdba_dev_status(dev)
    end

    return "ERROR: invalid field name: '" .. field .. "'"
end
