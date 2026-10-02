-- PlayerbotRaidFinder: addon protocol V1.
-- Requests go out with prefix "RFC" as a whisper to ourselves; replies come back
-- with prefix "RF". Using two prefixes means our own echoed request is never
-- mistaken for a server reply.
local RF = PlayerbotRaidFinder

local Net = {}
RF.Net = Net

local CLIENT_PREFIX = "RFC"
local SERVER_PREFIX = "RF"
local VERSION = "V1"

local function Send(body)
    SendAddonMessage(CLIENT_PREFIX, VERSION .. ":" .. body, "WHISPER", UnitName("player"))
end

function Net:RequestList()   Send("LIST") end
function Net:RequestStatus() Send("STATUS") end
function Net:Leave()         Send("LEAVE") end
function Net:Accept()        Send("ACCEPT") end
function Net:Decline()       Send("DECLINE") end

function Net:Join(raidId, role)
    if not raidId or not role then
        return
    end
    -- Only [A-Z0-9_] ever leaves the client; the server validates again anyway.
    raidId = raidId:gsub("[^%w_]", "")
    role = role:gsub("[^%w_]", "")
    Send("JOIN:" .. raidId .. ":" .. role)
end

local function Split(msg)
    local parts = {}
    for field in string.gmatch(msg .. ":", "([^:]*):") do
        table.insert(parts, field)
    end
    return parts
end

local function Dispatch(msg)
    local p = Split(msg)
    if p[1] ~= VERSION then
        return
    end
    local kind = p[2]
    if kind == "RAIDS" then
        if p[3] == "BEGIN" then RF:OnRaidsBegin() else RF:OnRaidsEnd() end
    elseif kind == "RAID" then
        -- Display name is the last field and may itself contain ':'.
        local name = table.concat(p, ":", 10)
        RF:OnRaid(p[3], p[4], p[5], p[6], p[7], p[8], p[9], name)
    elseif kind == "ME" then
        RF:OnMe(p[3], p[4])
    elseif kind == "STATE" then
        RF:OnState(p[3], p[4], p[5], p[6], p[7])
    elseif kind == "COMP" then
        RF:OnComp(p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11])
    elseif kind == "PROPOSAL" then
        RF:OnProposal(p[3], p[4], p[5], p[6], p[7])
    elseif kind == "MEMBER" then
        RF:OnMember(p[3], p[4])
    elseif kind == "ERROR" then
        RF:OnError(p[3])
    elseif kind == "INFO" then
        RF:OnInfo(p[3])
    end
end

local frame = CreateFrame("Frame")
frame:RegisterEvent("CHAT_MSG_ADDON")
frame:SetScript("OnEvent", function(_, _, prefix, msg, _, sender)
    if prefix ~= SERVER_PREFIX or sender ~= UnitName("player") then
        return
    end
    Dispatch(msg)
end)
