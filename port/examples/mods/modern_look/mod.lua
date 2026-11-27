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
-- vibrance, contrast, balance: how much of the scene's colour cast is taken out) and the shadows' tint {r, g, b}.
local NOON = { saturation = 110, contrast = 110 }
local EVENING = { saturation = 115, vibrance = 115, balance = 35, shadow_tint = { -0.1, -0.35, 0.7 } }
local NIGHT = { saturation = 95, contrast = 115, balance = 60, shadow_tint = { -0.5, -0.3, 1.0 } }
local MAP_LOOKS = {
    [0] = NOON,                                                                        -- Wasteland - Noon
    [1] = NOON,                                                                        -- Rocky Area - Noon
    [2] = { saturation = 115, vibrance = 120, balance = 55, shadow_tint = { -0.5, 0.1, 0.6 } },   -- Planet Namek
    [3] = { saturation = 110, contrast = 125, balance = 25, shadow_tint = { 0.2, -0.5, 0.3 } },   -- Dying Namek
    [4] = NOON,                                                                        -- World Tournament - Noon
    [5] = { saturation = 110, contrast = 105, vibrance = 115 },                        -- Kami's Lookout
    [6] = EVENING,                                                                     -- Cell Games Arena - Evening
    [7] = { saturation = 120, vibrance = 130 },                                        -- Supreme Kai's World
    [8] = { saturation = 90, contrast = 115, balance = 100 },                          -- Hyperbolic Time Chamber
    [9] = NOON,                                                                        -- City Ruins - Noon
    [10] = NOON,                                                                       -- Mountain Road - Noon
    [11] = { saturation = 120, vibrance = 135, contrast = 105 },                       -- Islands
    [12] = { saturation = 120, vibrance = 135, contrast = 105 },                       -- Kame House
    [13] = NIGHT,                                                                      -- Planet - Night
    [14] = { saturation = 105, contrast = 110, shadow_tint = { -0.6, -0.1, 1.0 } },   -- Glacier
    [15] = { saturation = 85, contrast = 120, balance = 80 },                          -- Ruined Earth
    [16] = { saturation = 110, contrast = 120, balance = 30 },                         -- Outer Space
    [17] = { saturation = 115, vibrance = 125 },                                       -- Penguin Village
    [18] = { saturation = 110, contrast = 115, balance = 30, shadow_tint = { 0.3, -0.4, 0.4 } },  -- Hell
    [19] = { saturation = 100, contrast = 110, balance = 140 },                        -- Desert - Noon
    [20] = NOON,                                                                       -- King's Castle
    [21] = { saturation = 105, contrast = 110, shadow_tint = { -0.6, -0.1, 1.0 } },   -- Muscle Tower (snow)
    [22] = { saturation = 115, vibrance = 125 },                                       -- Mount Paozu
    [23] = EVENING, [25] = EVENING, [27] = EVENING, [29] = EVENING, [31] = EVENING, [33] = EVENING, [34] = EVENING,
    [24] = NIGHT, [26] = NIGHT, [30] = NIGHT, [32] = NIGHT,
    [28] = NOON,                                                                       -- Cell Game - Noon
}

local function slider(id, label, default, tip)
    return { id = id, label = label, type = "slider", min = 0, max = 100, step = 5, default = default, tip = tip }
end

return {
    name = "Modern look",
    version = "1.1",
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
    },

    -- Asked every blank: switching the mod off (F1 > Mods) gives the game's own picture back at once.
    on_frame = function(game)
        local m = bt3.setting("map_looks") and game.stage and MAP_LOOKS[game.stage]
        if m then
            bt3.look {
                saturation = m.saturation, vibrance = m.vibrance, contrast = m.contrast, balance = m.balance,
                shadow_tint = m.shadow_tint,
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
