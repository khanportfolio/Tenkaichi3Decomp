-- Modern look: the vivid, luminous style of later anime games (Sparking Zero, the anime of Dragon Ball Super),
-- drawn by the renderer over the game's own picture:
--   - anime shading: the fighters' two-tone shading with cool, coloured shadows and a soft short edge, instead of
--     the PS2's blurred ramp; the colours stay each fighter's, so it suits any model or costume mod;
--   - rim light: a thin edge of light along the fighters' silhouettes, stronger on the side facing the light;
--   - vivid colours: livelier colours (the dull ones more; skin and sand less), a little more contrast, cool
--     shadows, and part of the stage's own colour cast taken out so a warm stage does not go orange;
--   - ambient shadows: soft shadows in creases and where rocks and fighters meet the ground;
--   - brighter energy: ki blasts, beams and sparks stronger and more saturated;
--   - energy light: that light tinting the ground, the rocks and the fighters round it.
--   - atmosphere: what is far fades softly into the air, in each stage's own colour;
--   - sun rays: shafts of light from the sky past the fighters and the rocks, in each stage's own light;
--   - map looks (27 Nov 2026): each of the game's stages its own colours, as an anime colourist would set them:
--     clear and crisp at noon, the sunset's warmth kept in the evening, cool deep blue at night, Namek's green,
--     the glacier icy, Hell and Dying Namek dramatic, Ruined Earth grey and harsh.
-- Only the picture changes: the fight, replays and online play are the same with or without it.
-- Vivid colours, ambient shadows and the energy light need the GPU renderer (F1 > Video), not OpenGL.
-- (The BT4decomp port's "Effects" settings, written as a script mod: docs/port/mods.md, bt3.look.)

local RIM_COLOURS = {
    0xFFECD6FF, -- warm white (the BT4 port's)
    0xD8ECFFFF, -- cool white
    0xFFD27AFF, -- gold
    0x8FD8FFFF, -- cyan
}

-- By stage number (game.stage, the game's stage list): percentages of the vivid colours' own values (saturation,
-- vibrance, contrast, balance: how much of the scene's colour cast is taken out) and the shadows' tint {r, g, b};
-- the atmosphere (haze); the sun rays (28 Nov: how strong, where the light comes from in percent of the picture,
-- its colour; F1 > Effects > Sun rays switches them off). None indoors, in space or in Hell.
local NOON = { saturation = 100, contrast = 105, balance = 140,                   -- (27 Nov: neutral, no orange cast)
               haze = 45, haze_start = 650, haze_color = 0xCFE3F5FF,
               sun_rays = 0, sun_x = 65, sun_y = -15, sun_color = 0xFFF4DCFF }  -- (high, warm white)
local EVENING = { saturation = 115, vibrance = 115, balance = 35, shadow_tint = { -0.1, -0.35, 0.7 },
                  haze = 50, haze_start = 650, haze_color = 0xF0C3A6FF,
                  sun_rays = 0, sun_x = 75, sun_y = 5, sun_color = 0xFFB878FF }         -- (low, orange)
local NIGHT = { saturation = 100, contrast = 85, balance = 40, shadow_tint = { -0.3, -0.15, 0.6 },  -- (115 crushed the ground)
                haze = 50, haze_start = 650, haze_color = 0x1E2A4AFF,
                sun_rays = 0, sun_x = 30, sun_y = -15, sun_color = 0x9DB8FFFF }       -- (faint moonlight)
local function with(base, extra)
    local t = {}
    for k, v in pairs(base) do t[k] = v end
    for k, v in pairs(extra) do t[k] = v end
    return t
end
local MAP_LOOKS = {
    [0] = NOON,                                                                        -- Wasteland - Noon
    [1] = NOON,                                                                        -- Rocky Area - Noon
    [2] = { saturation = 85, vibrance = 60, balance = 55, shadow_tint = { -0.5, 0.1, 0.6 },       -- Planet Namek
            haze = 45, haze_start = 650, haze_color = 0xBDEBD3FF, sun_rays = 0, sun_color = 0xE6FFE8FF },
    [3] = { saturation = 110, contrast = 125, balance = 25, shadow_tint = { 0.2, -0.5, 0.3 },     -- Dying Namek
            haze = 50, haze_start = 600, haze_color = 0x8A3A3AFF },
    [4] = NOON,                                                                        -- World Tournament - Noon
    [5] = with(NOON, { vibrance = 115, haze_color = 0xE3EEFAFF }),                     -- Kami's Lookout
    [6] = EVENING,                                                                     -- Cell Games Arena - Evening
    [7] = { saturation = 110, vibrance = 120, haze = 40, haze_start = 650, haze_color = 0xD9E8D0FF }, -- Supreme Kai's World
    [8] = { saturation = 90, contrast = 115, balance = 100 },                          -- Hyperbolic Time Chamber
    [9] = NOON,                                                                        -- City Ruins - Noon
    [10] = NOON,                                                                       -- Mountain Road - Noon
    [11] = with(NOON, { saturation = 115, vibrance = 125, haze_color = 0xCFEAF7FF }),  -- Islands
    [12] = with(NOON, { saturation = 115, vibrance = 125, haze_color = 0xCFEAF7FF }),  -- Kame House
    [13] = NIGHT,                                                                      -- Planet - Night
    [14] = { saturation = 100, contrast = 110, shadow_tint = { -0.6, -0.1, 1.0 },      -- Glacier
             haze = 55, haze_start = 600, haze_color = 0xDCEBFAFF, sun_rays = 0, sun_color = 0xF2F8FFFF },
    [15] = { saturation = 85, contrast = 120, balance = 80, haze = 50, haze_start = 600, haze_color = 0x8C8478FF }, -- Ruined Earth
    [16] = { saturation = 110, contrast = 120, balance = 30 },                         -- Outer Space
    [17] = with(NOON, { saturation = 110, vibrance = 120 }),                           -- Penguin Village
    [18] = { saturation = 110, contrast = 115, balance = 30, shadow_tint = { 0.3, -0.4, 0.4 },    -- Hell
             haze = 45, haze_start = 650, haze_color = 0x7A2E22FF },
    [19] = with(NOON, { balance = 160, haze_color = 0xEFE0C4FF }),                     -- Desert - Noon
    [20] = NOON,                                                                       -- King's Castle
    [21] = { saturation = 100, contrast = 110, shadow_tint = { -0.6, -0.1, 1.0 },      -- Muscle Tower (snow)
             haze = 55, haze_start = 600, haze_color = 0xDCEBFAFF },
    [22] = with(NOON, { saturation = 110, vibrance = 120 }),                           -- Mount Paozu
    [23] = EVENING, [25] = EVENING, [27] = EVENING, [29] = EVENING, [31] = EVENING, [33] = EVENING, [34] = EVENING,
    [24] = NIGHT, [26] = NIGHT, [30] = NIGHT, [32] = NIGHT,
    [28] = NOON,                                                                       -- Cell Game - Noon
}

local function slider(id, label, default, tip)
    return { id = id, label = label, type = "slider", min = 0, max = 100, step = 5, default = default, tip = tip }
end

return {
    name = "Modern look",
    version = "1.3",
    author = "BT4decomp",
    description = "Anime shading, rim light, vivid colours, ambient shadows and glowing energy.",
    settings = {
        slider("shading", "Anime shading", 100, "Cool coloured shadows with a soft edge on the fighters."),
        slider("rim", "Rim light", 100, "Light along the fighters' silhouettes."),
        { id = "rim_colour", label = "Rim light colour", type = "choice",
          choices = { "Warm white", "Cool white", "Gold", "Cyan" }, default = 1 },
        slider("vivid", "Vivid colours", 100, "Livelier colours and a neutral balance (GPU renderer)."),
        slider("ambient", "Ambient shadows", 100, "Soft shadows in corners and at the foot of things (GPU renderer)."),
        slider("energy", "Brighter energy", 100, "Ki blasts, beams and sparks stronger and more saturated."),
        slider("energy_light", "Energy light", 0, "Energy lights up the ground and fighters round it (GPU renderer)."),
        { id = "map_looks", label = "Map looks", type = "toggle", default = true,
          tip = "Each stage its own colours: crisp noon, warm evening, deep blue night (GPU renderer)." },
        { id = "atmosphere", label = "Atmosphere", type = "toggle", default = true,
          tip = "Distant scenery fades softly into the air, each stage in its own colour (GPU renderer)." },
    },

    -- Asked every blank: switching the mod off (F1 > Mods) gives the game's own picture back at once.
    on_frame = function(game)
        local m = bt3.setting("map_looks") and game.stage and MAP_LOOKS[game.stage]
        if m then
            local air = bt3.setting("atmosphere")
            bt3.look {
                saturation = m.saturation, vibrance = m.vibrance, contrast = m.contrast, balance = m.balance,
                shadow_tint = m.shadow_tint,
                haze = air and m.haze or nil, haze_start = air and m.haze_start or nil,
                haze_color = air and m.haze_color or nil,
                sun_rays = m.sun_rays, sun_x = m.sun_x, sun_y = m.sun_y, sun_color = m.sun_color,
            }
        end
        bt3.look {
            shading = bt3.setting("shading"),
            rim = bt3.setting("rim"),
            rim_color = RIM_COLOURS[bt3.setting("rim_colour")] or RIM_COLOURS[1],
            vivid = bt3.setting("vivid"),
            ambient = bt3.setting("ambient"),
            energy = bt3.setting("energy"),
            energy_light = bt3.setting("energy_light"),
        }
    end,
}
