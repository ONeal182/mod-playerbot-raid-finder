-- PlayerbotRaidFinder: WotLK-style UI (dialog frame, dropdown, role icons,
-- proposal popup). Only Blizzard 3.3.5 templates and textures are used.
local RF = PlayerbotRaidFinder
local L = RF.L

local UI = {}
RF.UI = UI

-- Region:SetSize is not reliable on every 3.3.5 client build.
local function Size(region, w, h)
    region:SetWidth(w)
    region:SetHeight(h)
end

local ROLE_ORDER = { "TANK", "HEAL", "DPS" }
local ROLE_ICON = { TANK = "TANK", HEAL = "HEALER", DPS = "DAMAGER" }
local ROLE_TEXTURE = "Interface\\LFGFrame\\UI-LFG-ICON-ROLES"
local CHECK = "|TInterface\\RAIDFRAME\\ReadyCheck-Ready:14:14|t"
local WAIT = "|TInterface\\RAIDFRAME\\ReadyCheck-Waiting:14:14|t"

local function SetRoleIcon(texture, role)
    texture:SetTexture(ROLE_TEXTURE)
    if GetTexCoordsForRole then
        texture:SetTexCoord(GetTexCoordsForRole(ROLE_ICON[role]))
    end
end

local function MakeDialog(name, width, height, title)
    local f = CreateFrame("Frame", name, UIParent)
    Size(f, width, height)
    f:SetFrameStrata("DIALOG")
    f:SetToplevel(true)
    f:EnableMouse(true)
    f:SetMovable(true)
    f:RegisterForDrag("LeftButton")
    f:SetScript("OnDragStart", f.StartMoving)
    f:SetScript("OnDragStop", f.StopMovingOrSizing)
    f:SetBackdrop({
        bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
        edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
        tile = true, tileSize = 32, edgeSize = 32,
        insets = { left = 11, right = 12, top = 12, bottom = 11 },
    })

    local header = f:CreateTexture(nil, "ARTWORK")
    header:SetTexture("Interface\\DialogFrame\\UI-DialogBox-Header")
    Size(header, 300, 64)
    header:SetPoint("TOP", 0, 12)
    local titleText = f:CreateFontString(nil, "ARTWORK", "GameFontNormal")
    titleText:SetPoint("TOP", header, "TOP", 0, -14)
    titleText:SetText(title)
    f.title = titleText
    return f
end

local function MakeButton(parent, text, width)
    local b = CreateFrame("Button", nil, parent, "UIPanelButtonTemplate")
    Size(b, width or 120, 22)
    b:SetText(text)
    return b
end

local function Line(parent, font)
    local fs = parent:CreateFontString(nil, "ARTWORK", font or "GameFontHighlight")
    fs:SetJustifyH("LEFT")
    return fs
end

local function SetRoleBar(bar, have, need)
    bar:SetMinMaxValues(0, math.max(need, 1))
    bar:SetValue(math.min(have, need))
    if need > 0 and have >= need then
        bar:SetStatusBarColor(0.1, 0.8, 0.1)
    else
        bar:SetStatusBarColor(0.9, 0.7, 0.0)
    end
    bar.value:SetText(string.format("%d / %d", have, need))
end

local function Clock(sec)
    sec = math.max(0, sec or 0)
    return string.format("%02d:%02d", math.floor(sec / 60), sec % 60)
end

local function DifficultyText(raid)
    if not raid then return "" end
    local heroic = raid.id:find("H$") ~= nil
    return string.format(heroic and L.PLAYER_HEROIC or L.PLAYER_NORMAL, raid.size)
end

-- ------------------------------------------------------------------ main frame

function UI:Build()
    if self.frame then return end

    local f = MakeDialog("PlayerbotRaidFinderFrame", 340, 380, L.TITLE)
    f:SetPoint("CENTER", -150, 40)
    f:Hide()
    tinsert(UISpecialFrames, "PlayerbotRaidFinderFrame")
    self.frame = f

    local close = CreateFrame("Button", nil, f, "UIPanelCloseButton")
    close:SetPoint("TOPRIGHT", -6, -6)

    -- Selection panel -------------------------------------------------------
    local sel = CreateFrame("Frame", nil, f)
    sel:SetPoint("TOPLEFT", 20, -30)
    sel:SetPoint("BOTTOMRIGHT", -20, 20)
    self.selPanel = sel

    local raidLabel = Line(sel, "GameFontNormal")
    raidLabel:SetPoint("TOPLEFT", 0, -6)
    raidLabel:SetText(L.RAID)

    local dd = CreateFrame("Frame", "PlayerbotRaidFinderRaidDropDown", sel, "UIDropDownMenuTemplate")
    dd:SetPoint("TOPLEFT", raidLabel, "BOTTOMLEFT", -16, -4)
    UIDropDownMenu_SetWidth(dd, 250)
    -- Two levels: expansion, then its raids (keeps every list short).
    UIDropDownMenu_Initialize(dd, function(_, level)
        level = level or 1
        if level == 1 then
            for tier = 0, 2 do
                local has = false
                for _, raid in ipairs(RF.raids) do
                    if raid.tier == tier then has = true end
                end
                if has then
                    local info = UIDropDownMenu_CreateInfo()
                    info.text = L.TIERS[tier]
                    info.value = tier
                    info.hasArrow = true
                    info.notCheckable = true
                    UIDropDownMenu_AddButton(info, level)
                end
            end
        else
            local tier = UIDROPDOWNMENU_MENU_VALUE
            local playerLevel = UnitLevel("player")
            for _, raid in ipairs(RF.raids) do
                if raid.tier == tier then
                    local info = UIDropDownMenu_CreateInfo()
                    local color = playerLevel >= raid.minLevel and "" or "|cff808080"
                    local req = raid.minGear > 0 and ("  |cff9d9d9dilvl " .. raid.minGear .. "+|r") or ""
                    info.text = color .. raid.name .. (raid.experimental and " |cffff8800*|r" or "") .. req
                    info.value = raid.id
                    info.checked = (RF.selectedRaid == raid.id)
                    info.func = function()
                        RF.selectedRaid = raid.id
                        CloseDropDownMenus()
                        UI:Refresh()
                    end
                    UIDropDownMenu_AddButton(info, level)
                end
            end
        end
    end)
    self.raidDropDown = dd

    local roleLabel = Line(sel, "GameFontNormal")
    roleLabel:SetPoint("TOPLEFT", raidLabel, "BOTTOMLEFT", 0, -48)
    roleLabel:SetText(L.ROLE)

    self.roleButtons = {}
    for i, role in ipairs(ROLE_ORDER) do
        local b = CreateFrame("Button", nil, sel)
        Size(b, 56, 56)
        b:SetPoint("TOPLEFT", roleLabel, "BOTTOMLEFT", 10 + (i - 1) * 90, -8)
        local icon = b:CreateTexture(nil, "ARTWORK")
        icon:SetAllPoints()
        SetRoleIcon(icon, role)
        b.icon = icon
        b:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square", "ADD")
        local check = b:CreateTexture(nil, "OVERLAY")
        check:SetTexture("Interface\\Buttons\\UI-CheckBox-Check")
        Size(check, 26, 26)
        check:SetPoint("BOTTOMRIGHT", 6, -6)
        b.check = check
        local label = Line(b, "GameFontHighlightSmall")
        label:SetPoint("TOP", b, "BOTTOM", 0, -2)
        label:SetText(L[role])
        b:SetScript("OnClick", function()
            RF.selectedRole = role
            PlaySound("igMainMenuOptionCheckBoxOn")
            UI:Refresh()
        end)
        self.roleButtons[role] = b
    end

    local gear = Line(sel, "GameFontNormalSmall")
    gear:SetPoint("TOPLEFT", roleLabel, "BOTTOMLEFT", 0, -96)
    self.gearText = gear

    local info = Line(sel, "GameFontHighlightSmall")
    info:SetPoint("TOPLEFT", gear, "BOTTOMLEFT", 0, -6)
    info:SetWidth(290)
    self.infoText = info

    local find = MakeButton(sel, L.FIND_RAID, 140)
    find:SetPoint("BOTTOM", 0, 6)
    find:SetScript("OnClick", function()
        if RF.selectedRaid and RF.selectedRole then
            RF.Net:Join(RF.selectedRaid, RF.selectedRole)
        end
    end)
    self.findButton = find

    -- Status panel ----------------------------------------------------------
    local st = CreateFrame("Frame", nil, f)
    st:SetPoint("TOPLEFT", 20, -30)
    st:SetPoint("BOTTOMRIGHT", -20, 20)
    st:Hide()
    self.statusPanel = st

    local raidName = Line(st, "GameFontNormalLarge")
    raidName:SetPoint("TOP", 0, -6)
    raidName:SetJustifyH("CENTER")
    self.stRaid = raidName

    local stateText = Line(st, "GameFontHighlight")
    stateText:SetPoint("TOP", raidName, "BOTTOM", 0, -8)
    stateText:SetJustifyH("CENTER")
    self.stState = stateText

    self.stRoles = {}
    for i, role in ipairs(ROLE_ORDER) do
        local icon = st:CreateTexture(nil, "ARTWORK")
        Size(icon, 32, 32)
        icon:SetPoint("TOPLEFT", 4, -58 - (i - 1) * 44)
        SetRoleIcon(icon, role)

        local name = Line(st, "GameFontNormal")
        name:SetPoint("TOPLEFT", icon, "TOPRIGHT", 8, 1)
        name:SetText(L[role])

        local bar = CreateFrame("StatusBar", nil, st)
        Size(bar, 220, 14)
        bar:SetPoint("TOPLEFT", name, "BOTTOMLEFT", 0, -3)
        bar:SetStatusBarTexture("Interface\\TargetingFrame\\UI-StatusBar")
        bar:SetStatusBarColor(0.1, 0.8, 0.1)
        bar:SetMinMaxValues(0, 1)
        bar:SetValue(0)
        local bg = bar:CreateTexture(nil, "BACKGROUND")
        bg:SetAllPoints()
        bg:SetTexture(0, 0, 0, 0.5)
        bar:SetBackdrop({
            edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border", edgeSize = 8,
            insets = { left = 2, right = 2, top = 2, bottom = 2 },
        })
        local value = bar:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        value:SetPoint("CENTER", bar, "CENTER", 0, 0)
        bar.value = value
        self.stRoles[role] = bar
    end

    local counts = Line(st, "GameFontHighlight")
    counts:SetPoint("TOPLEFT", 6, -196)
    counts:SetWidth(290)
    counts:SetJustifyV("TOP")
    self.stCounts = counts

    local leave = MakeButton(st, L.LEAVE_QUEUE, 140)
    leave:SetPoint("BOTTOM", 0, 6)
    leave:SetScript("OnClick", function() RF.Net:Leave() end)
    self.leaveButton = leave

    f:SetScript("OnUpdate", function(_, dt)
        UI.tick = (UI.tick or 0) + dt
        if UI.tick >= 1 then
            UI.tick = 0
            UI:Refresh()
        end
    end)
    f:SetScript("OnShow", function() RF.Net:RequestStatus() end)
end

-- ------------------------------------------------------------ proposal popup

function UI:BuildProposal()
    if self.proposal then return end

    local p = MakeDialog("PlayerbotRaidFinderProposal", 300, 250, L.RAID_FOUND)
    p:SetPoint("CENTER", 0, 120)
    p:Hide()
    self.proposal = p

    local name = Line(p, "GameFontNormalLarge")
    name:SetPoint("TOP", 0, -30)
    name:SetJustifyH("CENTER")
    p.name = name

    local diff = Line(p, "GameFontHighlight")
    diff:SetPoint("TOP", name, "BOTTOM", 0, -4)
    p.diff = diff

    local comp = Line(p, "GameFontHighlight")
    comp:SetPoint("TOP", diff, "BOTTOM", 0, -12)
    comp:SetJustifyH("CENTER")
    p.comp = comp

    local members = Line(p, "GameFontHighlightSmall")
    members:SetPoint("TOP", comp, "BOTTOM", 0, -10)
    members:SetWidth(260)
    members:SetJustifyH("CENTER")
    p.members = members

    local timer = Line(p, "GameFontNormalSmall")
    timer:SetPoint("BOTTOM", 0, 44)
    p.timer = timer

    local accept = MakeButton(p, L.ACCEPT, 110)
    accept:SetPoint("BOTTOMRIGHT", p, "BOTTOM", -6, 16)
    accept:SetScript("OnClick", function() RF.Net:Accept() end)
    p.accept = accept

    local decline = MakeButton(p, L.DECLINE, 110)
    decline:SetPoint("BOTTOMLEFT", p, "BOTTOM", 6, 16)
    decline:SetScript("OnClick", function() RF.Net:Decline() end)
    p.decline = decline

    p:SetScript("OnUpdate", function(_, dt)
        p.tick = (p.tick or 0) + dt
        if p.tick >= 0.5 then
            p.tick = 0
            UI:RefreshProposal()
        end
    end)
end

function UI:RefreshProposal()
    local s = RF.status
    local p = self.proposal
    if s.state ~= "PROPOSAL" then
        if p:IsShown() then p:Hide() end
        return
    end
    if not p:IsShown() then p:Show() end

    local raid = RF.raidsById[s.raidId]
    p.name:SetText(raid and raid.name or s.raidId)
    p.diff:SetText(DifficultyText(raid))
    p.comp:SetText(string.format("%s %d/%d   %s %d/%d   %s %d/%d", L.TANK, s.tank, s.tankNeed, L.HEAL, s.heal,
        s.healNeed, L.DPS, s.dps, s.dpsNeed))

    if s.meAccepted then
        local lines = { L.WAITING, "" }
        for _, m in ipairs(s.members) do
            table.insert(lines, (m.accepted and CHECK or WAIT) .. " " .. m.name)
        end
        table.insert(lines, "")
        table.insert(lines, L.BOTS_READY)
        p.members:SetText(table.concat(lines, "\n"))
        p.accept:Disable()
    else
        p.members:SetText(string.format(L.REAL_PLAYERS, s.real) .. "    " .. string.format(L.PLAYERBOTS, s.bots))
        p.accept:Enable()
    end
    p.timer:SetText(string.format(L.TIME_LEFT, RF:ProposalSecondsLeft()))
end

-- ----------------------------------------------------------------- refreshing

function UI:Refresh()
    self:Build()
    self:BuildProposal()
    self:RefreshProposal()

    local s = RF.status
    if s.state == "IDLE" then
        self.selPanel:Show()
        self.statusPanel:Hide()
        self:RefreshSelection()
    else
        self.selPanel:Hide()
        self.statusPanel:Show()
        self:RefreshStatus()
    end
end

function UI:RefreshSelection()
    local raid = RF.selectedRaid and RF.raidsById[RF.selectedRaid]
    UIDropDownMenu_SetText(self.raidDropDown, raid and raid.name or L.NO_RAIDS)

    for role, b in pairs(self.roleButtons) do
        if RF.selectedRole == role then
            b.check:Show()
            b.icon:SetDesaturated(false)
        else
            b.check:Hide()
            b.icon:SetDesaturated(RF.selectedRole ~= nil)
        end
    end

    local gearOk = true
    local levelOk = not raid or UnitLevel("player") >= raid.minLevel
    local gear
    if raid and raid.minGear > 0 then
        local mine = RF.myItemLevel
        gearOk = mine == nil or mine >= raid.minGear
        gear = (gearOk and "|cffffffff" or "|cffff2020") .. raid.minGear .. "+|r"
        if mine then
            gear = gear .. "  (" .. string.format(L.YOUR_ILVL, mine) .. ")"
        end
    else
        gear = "|cffffffff" .. L.NO_GEAR_REQ .. "|r"
    end
    self.gearText:SetText(L.RECOMMENDED_GEAR .. " " .. gear)

    local info = {}
    if raid then
        table.insert(info, DifficultyText(raid) .. "  -  " .. string.format(L.LEVEL_REQ, raid.minLevel))
        if raid.experimental then table.insert(info, "|cffff8800" .. L.EXPERIMENTAL .. "|r") end
    end
    if not levelOk then table.insert(info, "|cffff2020" .. L.LEVEL_TOO_LOW_UI .. "|r") end
    if not gearOk then table.insert(info, "|cffff2020" .. L.ILVL_TOO_LOW .. "|r") end
    if not RF.selectedRole then table.insert(info, L.PICK_ROLE) end
    self.infoText:SetText(table.concat(info, "\n"))

    if raid and RF.selectedRole and gearOk and levelOk then
        self.findButton:Enable()
    else
        self.findButton:Disable()
    end
end

function UI:RefreshStatus()
    local s = RF.status
    local raid = RF.raidsById[s.raidId]
    self.stRaid:SetText(raid and raid.name or s.raidId)
    self.stState:SetText(RF:StateText(s.state))

    SetRoleBar(self.stRoles.TANK, s.tank, s.tankNeed)
    SetRoleBar(self.stRoles.HEAL, s.heal, s.healNeed)
    SetRoleBar(self.stRoles.DPS, s.dps, s.dpsNeed)

    local lines
    if s.state == "INRAID" then
        lines = {
            string.format(L.PLAYERS, s.real + s.bots, s.tankNeed + s.healNeed + s.dpsNeed),
            string.format(L.REAL, s.real),
            string.format(L.BOTS, s.bots),
            string.format(L.STATUS, L.STATE_INRAID),
        }
        self.leaveButton:SetText(L.LEAVE_RAID)
    else
        lines = {
            string.format(L.REAL_PLAYERS, s.real),
            string.format(L.PLAYERBOTS, s.bots),
        }
        if s.state == "SEARCHING" or s.state == "FILLING" then
            table.insert(lines, string.format(L.BOTS_REQUIRED, s.botsRequired))
            table.insert(lines, string.format(L.QUEUE_TIME, Clock(RF:QueueSeconds())))
        end
        self.leaveButton:SetText(L.LEAVE_QUEUE)
    end
    self.stCounts:SetText(table.concat(lines, "\n"))
end

function UI:Toggle()
    self:Build()
    if self.frame:IsShown() then
        self.frame:Hide()
    else
        self.frame:Show()
        RF.Net:RequestList()
        self:Refresh()
    end
end

-- ------------------------------------------------------------- minimap button
-- Lives in UI.lua (not only Minimap.lua) so it also appears after a /reload on
-- a client that cached the older .toc file list.
local Button = {}
RF.MinimapButton = Button

local RADIUS = 80

local function DB()
    PlayerbotRaidFinderDB = PlayerbotRaidFinderDB or {}
    if PlayerbotRaidFinderDB.minimapAngle == nil then
        PlayerbotRaidFinderDB.minimapAngle = 200
    end
    return PlayerbotRaidFinderDB
end

local function Place(btn)
    local angle = math.rad(DB().minimapAngle)
    btn:ClearAllPoints()
    btn:SetPoint("CENTER", Minimap, "CENTER", math.cos(angle) * RADIUS, math.sin(angle) * RADIUS)
end

local function OnDragUpdate(btn)
    local mx, my = Minimap:GetCenter()
    local cx, cy = GetCursorPosition()
    local scale = Minimap:GetEffectiveScale()
    cx, cy = cx / scale, cy / scale
    DB().minimapAngle = math.deg(math.atan2(cy - my, cx - mx)) % 360
    Place(btn)
end

function Button:ResetPosition()
    DB().minimapAngle = 200
    if self.button then Place(self.button) end
end

function Button:Create()
    if self.button then return end

    local b = CreateFrame("Button", "PlayerbotRaidFinderMinimapButton", Minimap)
    b:SetWidth(31)
    b:SetHeight(31)
    b:SetFrameStrata("MEDIUM")
    b:SetFrameLevel(8)
    b:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    b:RegisterForDrag("LeftButton")
    b:SetHighlightTexture("Interface\\Minimap\\UI-Minimap-ZoomButton-Highlight")

    local icon = b:CreateTexture(nil, "BACKGROUND")
    icon:SetWidth(20)
    icon:SetHeight(20)
    icon:SetPoint("TOPLEFT", 7, -5)
    icon:SetTexture("Interface\\Icons\\INV_Misc_Head_Dragon_01")
    icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)

    local border = b:CreateTexture(nil, "OVERLAY")
    border:SetWidth(53)
    border:SetHeight(53)
    border:SetPoint("TOPLEFT")
    border:SetTexture("Interface\\Minimap\\MiniMap-TrackingBorder")

    local queued = b:CreateTexture(nil, "OVERLAY")
    queued:SetWidth(14)
    queued:SetHeight(14)
    queued:SetPoint("BOTTOMRIGHT", -4, 4)
    queued:SetTexture("Interface\\RAIDFRAME\\ReadyCheck-Waiting")
    queued:Hide()
    b.queued = queued

    b:SetScript("OnClick", function() RF.UI:Toggle() end)
    b:SetScript("OnDragStart", function(self)
        self:SetScript("OnUpdate", OnDragUpdate)
    end)
    b:SetScript("OnDragStop", function(self)
        self:SetScript("OnUpdate", nil)
    end)
    b:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_LEFT")
        GameTooltip:AddLine(L.TITLE)
        local s = RF.status
        if s.state ~= "IDLE" then
            local raid = RF.raidsById[s.raidId]
            GameTooltip:AddLine((raid and raid.name or s.raidId) .. ": " .. RF:StateText(s.state), 1, 1, 1)
        end
        GameTooltip:AddLine(L.MINIMAP_HINT, 0.7, 0.7, 0.7)
        GameTooltip:Show()
    end)
    b:SetScript("OnLeave", function() GameTooltip:Hide() end)

    self.button = b
    Place(b)
end

function Button:Refresh()
    if not self.button then return end
    if RF.status.state ~= "IDLE" then
        self.button.queued:Show()
    else
        self.button.queued:Hide()
    end
end

local loader = CreateFrame("Frame")
loader:RegisterEvent("PLAYER_LOGIN")
loader:RegisterEvent("PLAYER_ENTERING_WORLD")
loader:SetScript("OnEvent", function()
    Button:Create()
    Button:Refresh()
    UI:AttachToDungeonFinder()
end)

-- --------------------------------------------------- Dungeon Finder entry point
-- A "Raid Finder" button on the stock Dungeon Finder window (key I). Minimap
-- button collectors (HidingBar, SlideBar, MBB) cannot hide this one.
function UI:AttachToDungeonFinder()
    if self.lfdButton or not LFDParentFrame then return end
    local b = CreateFrame("Button", "PlayerbotRaidFinderLFDButton", LFDParentFrame, "UIPanelButtonTemplate")
    b:SetWidth(130)
    b:SetHeight(22)
    b:SetPoint("TOPRIGHT", LFDParentFrame, "TOPRIGHT", -36, -44)
    b:SetText(L.TITLE)
    b:SetScript("OnClick", function()
        HideUIPanel(LFDParentFrame)
        UI:Build()
        if not UI.frame:IsShown() then
            UI:Toggle()
        end
    end)
    self.lfdButton = b
end
