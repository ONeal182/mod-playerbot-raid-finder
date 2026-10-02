-- PlayerbotRaidFinder: state model and slash command.
-- The server is authoritative: this file only mirrors what the server reports.
local RF = PlayerbotRaidFinder
local L = RF.L

RF.raids = {}          -- ordered list of { id, size, minLevel, minGear, experimental, name }
RF.raidsById = {}
RF.pendingRaids = nil  -- list being received between RAIDS:BEGIN and RAIDS:END

RF.status = {
    state = "IDLE", raidId = "", role = "NONE", queueSec = 0, leader = false,
    tank = 0, tankNeed = 0, heal = 0, healNeed = 0, dps = 0, dpsNeed = 0,
    real = 0, bots = 0, botsRequired = 0,
    proposalLeft = 0, accepted = 0, totalReal = 0, meAccepted = false,
    members = {},
    receivedAt = 0,
}

RF.selectedRaid = nil
RF.selectedRole = nil

local function Print(msg)
    DEFAULT_CHAT_FRAME:AddMessage("|cff66ccff" .. L.ERR_PREFIX .. "|r" .. msg)
end
RF.Print = Print

function RF:StateText(state)
    return L["STATE_" .. (state or "IDLE")] or state
end

function RF:IsQueued()
    return self.status.state ~= "IDLE"
end

RF.myItemLevel = nil   -- server-computed average item level (same formula as the check)

function RF:OnMe(itemLevel, level)
    self.myItemLevel = tonumber(itemLevel)
    self:Refresh()
end

-- Called by Network.lua for every parsed server message.
function RF:OnRaidsBegin()
    self.pendingRaids = {}
end

function RF:OnRaid(id, size, minLevel, maxLevel, minGear, experimental, tier, name)
    local list = self.pendingRaids or self.raids
    table.insert(list, {
        id = id, size = tonumber(size) or 0, minLevel = tonumber(minLevel) or 0,
        maxLevel = tonumber(maxLevel) or 0, minGear = tonumber(minGear) or 0,
        experimental = experimental == "1", tier = tonumber(tier) or 2, name = name,
    })
end

function RF:OnRaidsEnd()
    if self.pendingRaids then
        self.raids = self.pendingRaids
        self.pendingRaids = nil
    end
    self.raidsById = {}
    for _, r in ipairs(self.raids) do
        self.raidsById[r.id] = r
    end
    if not self.selectedRaid or not self.raidsById[self.selectedRaid] then
        -- Default to the first raid the character can enter.
        local level = UnitLevel("player")
        self.selectedRaid = nil
        for _, r in ipairs(self.raids) do
            if level >= r.minLevel then
                self.selectedRaid = r.id
            end
        end
        if not self.selectedRaid and self.raids[1] then
            self.selectedRaid = self.raids[1].id
        end
    end
    self:Refresh()
end

function RF:OnState(state, raidId, role, queueSec, leader)
    local s = self.status
    local previous = s.state
    s.state = state
    s.raidId = raidId or ""
    s.role = role or "NONE"
    s.queueSec = tonumber(queueSec) or 0
    s.leader = leader == "1"
    s.receivedAt = GetTime()
    if state ~= "PROPOSAL" then
        s.members = {}
    end
    if state == "IDLE" then
        s.tank, s.heal, s.dps, s.real, s.bots, s.botsRequired = 0, 0, 0, 0, 0, 0
    end
    if state == "PROPOSAL" and previous ~= "PROPOSAL" then
        s.members = {}
        PlaySound("ReadyCheck")
    end
    self:Refresh()
end

function RF:OnComp(t, tn, h, hn, d, dn, real, bots, botsRequired)
    local s = self.status
    s.tank, s.tankNeed = tonumber(t) or 0, tonumber(tn) or 0
    s.heal, s.healNeed = tonumber(h) or 0, tonumber(hn) or 0
    s.dps, s.dpsNeed = tonumber(d) or 0, tonumber(dn) or 0
    s.real, s.bots, s.botsRequired = tonumber(real) or 0, tonumber(bots) or 0, tonumber(botsRequired) or 0
    self:Refresh()
end

function RF:OnProposal(raidId, secondsLeft, accepted, totalReal, meAccepted)
    local s = self.status
    s.proposalLeft = tonumber(secondsLeft) or 0
    s.proposalAt = GetTime()
    s.accepted = tonumber(accepted) or 0
    s.totalReal = tonumber(totalReal) or 0
    s.meAccepted = meAccepted == "1"
    s.members = {}
    self:Refresh()
end

function RF:OnMember(name, accepted)
    table.insert(self.status.members, { name = name, accepted = accepted == "1" })
    self:Refresh()
end

function RF:OnError(code)
    Print(L.ERR[code] or code)
    UIErrorsFrame:AddMessage(L.ERR[code] or code, 1.0, 0.1, 0.1, 1.0)
end

function RF:OnInfo(code)
    if code == "RELEASED" then
        Print(L.RELEASED)
    end
end

-- Seconds left on the proposal, counted down locally between server pushes.
function RF:ProposalSecondsLeft()
    local s = self.status
    if not s.proposalAt then
        return s.proposalLeft
    end
    local left = s.proposalLeft - (GetTime() - s.proposalAt)
    return left > 0 and math.floor(left) or 0
end

function RF:QueueSeconds()
    local s = self.status
    return s.queueSec + math.floor(GetTime() - (s.receivedAt or GetTime()))
end

function RF:Refresh()
    if self.UI and self.UI.Refresh then
        self.UI:Refresh()
    end
    if self.MinimapButton then
        self.MinimapButton:Refresh()
    end
end

-- Slash command: /rf toggles the window; /rf join ICC10 dps etc. for keyboard use.
SLASH_PLAYERBOTRAIDFINDER1 = "/rf"
SLASH_PLAYERBOTRAIDFINDER2 = "/raidfinder"
SlashCmdList["PLAYERBOTRAIDFINDER"] = function(msg)
    local cmd, a, b = strsplit(" ", strtrim(msg or ""))
    cmd = cmd and cmd:lower() or ""
    if cmd == "join" and a and b then
        RF.Net:Join(a:upper(), b:upper())
    elseif cmd == "leave" then
        RF.Net:Leave()
    elseif cmd == "accept" then
        RF.Net:Accept()
    elseif cmd == "decline" then
        RF.Net:Decline()
    elseif cmd == "button" then
        RF.MinimapButton:Create()
        RF.MinimapButton:ResetPosition()
        RF.MinimapButton.button:Show()
    else
        RF.UI:Toggle()
    end
end

-- Login / reload: ask the server for the raid list and our current state, so
-- the UI recovers after /reload, relog or a disconnect.
local boot = CreateFrame("Frame")
boot:RegisterEvent("PLAYER_ENTERING_WORLD")
boot:SetScript("OnEvent", function(self)
    self.elapsed = 0
    self:SetScript("OnUpdate", function(f, dt)
        f.elapsed = f.elapsed + dt
        if f.elapsed > 2 then
            f:SetScript("OnUpdate", nil)
            RF.Net:RequestList()
            RF.Net:RequestStatus()
        end
    end)
end)

-- Gear changed: ask the server for the fresh average item level (throttled).
local gearWatch = CreateFrame("Frame")
gearWatch:RegisterEvent("PLAYER_EQUIPMENT_CHANGED")
gearWatch:SetScript("OnEvent", function(self)
    self.wait = 1.5
    self:SetScript("OnUpdate", function(f, dt)
        f.wait = f.wait - dt
        if f.wait <= 0 then
            f:SetScript("OnUpdate", nil)
            RF.Net:RequestStatus()
        end
    end)
end)
