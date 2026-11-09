# Character packages (format 1)

A character package is a folder. A player drops it into `characters` next to the game and starts the game; there
is nothing to install, patch or rebuild. A package either **adds** a character to the roster or **replaces** one of
the game's own 161 characters, in whole or in part (a skin is a package with one file).

This page is the standard: what a package may hold, how the game reads it, and what it cannot do yet. It is
written for people who make packages. The working notes behind it are in `adding_characters.md`; the code is
`port/src/plat_charas.c`.

Contents: [1 Where packages go](#1-where-packages-go) · [2 The files](#2-the-files) ·
[3 character.txt](#3-charactertxt) · [4 Adding](#4-adding-a-character) · [5 Replacing](#5-replacing-a-character) ·
[6 Forms and fusions](#6-transformations-and-fusions) · [7 Numbers](#7-health-ki-blast-stocks) ·
[8 Where it shows](#8-where-a-package-shows) · [9 Limits](#9-limits) · [10 Not possible yet](#10-not-possible-yet) ·
[11 Online](#11-online-play) · [12 Starting from the game's own](#12-starting-from-the-games-own-characters) ·
[13 Testing](#13-testing-a-package) · [14 Examples](#14-examples) · [15 Older folders](#15-folders-made-before-format-1)

## 1. Where packages go

```
<the game>/
  characters/
    My Character/          one package
      character.txt
      ...
    Another One/           another
```

- The game looks for `characters` next to itself, then inside its data folder (`gamedata/characters`). The
  environment variable `BT3_CHARACTERS` names another folder.
- Every folder directly inside `characters` is looked at. A folder that holds none of the files below is passed
  over. Folders whose name starts with a dot, or is 64 characters or longer, are not read.
- Packages are read in the order of `characters/load_order.txt` (one folder a line, the Mod Manager's order; a
  folder can be switched off there), then the folders it does not name **in the order of their names**, without
  regard to upper and lower case. The order decides the ids of added characters (section 4) and which of two
  packages wins a conflict (section 5).
- The folder is read once, when the game starts. Change a package, start the game again. (Changes made in the Mod
  Manager take effect at once.)
- **The Mod Manager adds packages too** (main menu > Mod Manager): drop a package's folder, a folder of several, or
  an archive (zip, 7z, rar, tar) on the game's window, or pick one in its browser and press Square. Every package
  found is copied into `characters` (the original stays where it was); one already there with the same files is
  skipped, another of the same name gets " (2)". A folder name with accents is written with plain letters. Archives
  are unpacked by a program of the system: `tar` on Windows (zip on Windows 10, 7z and rar on Windows 11), on Linux
  `bsdtar`, `unzip`, `7z` or `unrar`, whichever is installed. Links in a package are not copied.
- F1 > Mods lists every package the game found, what it does, and anything wrong with it. The same remarks are in
  the game's log, each line starting `bt3: characters:`.

## 2. The files

All files are in the game's own formats, byte for byte what the disc holds for a character: the same files the
community's tools take out of the disc and put back. Nothing is converted. Names are matched **without regard to
case**; for the `.pak` files only the **ending** counts, so `Gokhan_0_1p.pak`, `skin_1P.PAK` and `x_1p.pak` are
all "the model of costume 1". If a folder has two files of one kind, the first by name is used.

| File | What it is | The disc's file | Largest size |
|---|---|---|---|
| `*_1p.pak` | model of costume 1 (with the character's own tables: moves, forms, the face on the fight's display) | `0x590 + id*10 + 0` | 1,572,864 bytes |
| `*_2p.pak`, `*_3p.pak`, `*_4p.pak` | models of costumes 2 to 4 | `+ 1`, `+ 2`, `+ 3` | 1,572,864 |
| `*_1p_dmg.pak` .. `*_4p_dmg.pak` | the damaged (torn clothes) model of each costume | `+ 4` .. `+ 7` | 1,572,864 |
| `*_anm.pak` | animations | `+ 8` | 1,443,840 |
| `*_eff.pak` | effects | `+ 9` | 845,824 |
| `*_voice_us.pak` | voice bank, **English** | `0xC7B + id` | 591,872 |
| `*_voice_jp.pak` | voice bank, **Japanese** | `0xBDA + id` | 591,872 |
| `voice_us/NNN.adx` | spoken lines, English: `000.adx` .. `099.adx` | `0xCC32 + id*100 + NNN` | none (streamed) |
| `voice_jp/NNN.adx` | spoken lines, Japanese | `0x8D4E + id*100 + NNN` | none (streamed) |
| `portrait.bin`, or `*_img.cdbt` | the large picture of the menus | `0x2F9 + id` | 92,160 |
| `member_1p.bin`, `member_2p.bin`, or `*_tag_P1.dbt`, `*_tag_P2.dbt` | the fight's member block (the small picture of a team mate), for the left and the right side | `8 + id*2 + side` | 4,096 |
| `chip.bin` | the small square of the roster: one entry of the character select's chip pack | menu archive `0x1C1`, section 45, entry `id + 1` | exactly the game's 5,504-byte entry |
| `character.txt` | the manifest (section 3) | | first 16,383 bytes are read |

Every file is optional, with one exception: a package that **adds** a character must have `*_1p.pak`.

Things to know:

- **"US" and "JP" are the community's names**, as its file lists have them: `_Voice_US` is the bank the game uses
  with the voice setting it starts with (English), `_Voice_JP` the other. A package made for the disc drops in
  unchanged.
- **A file that is too large is left out**, with a remark in the log and in F1 > Mods; the game then uses what it
  would have used without it (section 4, section 5). The sizes are the buffers the game reads each kind of file
  into; a file is read in whole 2,048-byte pieces, so a size is rounded up to the next multiple of 2,048 before it
  is compared. The original game's model buffer is 843,776 bytes; this game makes it as large as the largest model
  installed, up to the 1,572,864 above.
- **Empty files** (under 16 bytes) and things that are not plain files are left out the same way.
- **The game does not check what is inside a model, animation or effect file.** A model's own table names its
  animations and effects by number. A model put together with animations or effects it was not made for can stop
  the game in the middle of a fight. See "whole character or skin" in section 5.
- `chip.bin` is checked: it must be a texture file of the game whose offsets stay inside 5,504 bytes. Anything
  else is left out and the character keeps the chip it would have had.
- Folders inside a package other than `voice_us` and `voice_jp` are packages of their own (section 6).

## 3. character.txt

A text file, UTF-8, one `key=value` per line.

```
# a line that starts with # is a comment
format=1
name=Gokhan
form=Super Saiyan
base=12
author=Somebody
version=1.2
about=Fusion of two fighters, with his own moves and voice.
```

Rules of the reader, so that every tool and every hand writes files it accepts:

- Keys are matched without regard to case. Spaces around the key and around the value do not count.
- A line without `=` says nothing. An unknown key is ignored: a package made for a later format still loads, and
  you may keep notes of your own under keys of your own.
- If a key appears twice, the last one counts.
- Line ends may be `\n` or `\r\n`. A byte order mark at the start is skipped.
- Control characters in a value become spaces. Values are cut to their field (below) at a whole character.
- Only the first 16,383 bytes of the file are read.

| Key | Value | Meaning |
|---|---|---|
| `format` | `1` | The format this page describes. A higher number draws a remark; what the game knows is still read. |
| `name` | text, up to 63 bytes | The name on the roster screens. Without it: an added character is called by its folder's name; a replaced character keeps the disc's name. |
| `form` | text, up to 95 bytes; may be empty | The second line under the name ("Super Saiyan"). `form=` with nothing after it means "no label". Without the key: the folder's name for a form (a folder inside a character's folder), the base character's label for an added character, the disc's own for a replaced one. |
| `base` | disc character id, `0`..`160` | Adding only: the disc character this one stands on (section 4). Without it: `0`. |
| `replaces` | disc character id, `0`..`160` | Makes the package a **replacement** of that character (section 5). `base` is then that id, whatever `base=` says. |
| `author`, `version`, `about` | text (63, 31, 199 bytes) | Shown in F1 > Mods. Free text. |
| `left`, `up`, `right`, `down` | a link (section 6) | Where the four transformation inputs lead. |
| `fusion1`, `fusion2`, `fusion3` | a link | What the three fusions make. |
| `partner1`, `partner2`, `partner3` | up to four links, separated by commas | Who must be on the team for fusion 1, 2, 3. |
| `health` | `10000`..`150000` | Health, 10000 for each bar (section 7). |
| `ki` | `0`..`100000` | Ki at the start of a fight; 100000 is a full gauge. |
| `blast` | `0`..`7` | Number of blast stocks. |
| `damage` | `10`..`1000` | How hard the character's hits are, in percent of what they would do (section 7). |

Ids may be written in decimal or as `0x..`. A value that is not a number in its range is not used and draws a
remark. `port/tools/import_chara.py <game data> --names` prints the 161 ids with the names of your own disc.

A package needs no `character.txt` if the defaults fit: a folder with nothing but `X_1p.pak` in it adds a
character called by the folder's name, standing on character 0. Replacing needs the file, for `replaces=`.

## 4. Adding a character

A package **without** `replaces=` that has a `*_1p.pak` adds a character.

- **It gets an id from 198 (0xC6) up, in the order of the folder names**, forms right after their character. At
  most **57** added characters, forms included; a 58th is not loaded and F1 > Mods says so.
- **It gets a cell on the roster** after the game's own characters. A character that another added character
  transforms into shares that character's cell, as forms do in the original game.
- **It has as many costumes as it has model files in a row**: `_1p` alone is one costume, `_1p` and `_2p` two;
  `_1p` and `_3p` without `_2p` is one.
- **What the folder does not have comes from its base character**, the disc character named by `base=`:

| The folder has no ... | ... so the game uses |
|---|---|
| `*_Np_dmg.pak` for a costume it has | that costume's own model (the clothes do not tear) |
| `*_anm.pak`, `*_eff.pak` | the base character's |
| one of the two voice banks | the other language's bank of the folder, for both settings |
| both voice banks | the base character's |
| `voice_us/NNN.adx` | `voice_jp/NNN.adx` of the folder if it is there, else the base character's line; and the same the other way round |
| `portrait.bin`, `member_*.bin`, `chip.bin` | the base character's |

- "The base character's" file is always the disc's own file, also when another package replaces that
  character.
- **Everything that is a table of the game is the base character's**, because those tables have one row for each
  of the disc's 161 characters and no more: DP cost, AI behaviour, level, which opponents get a special line
  before a fight, the reference viewer's camera. Health, starting ki and blast stocks are the base's too unless
  the manifest says otherwise (section 7).
- **It is never locked.** The save has no place for it; a save made with packages installed loads in a game
  without them.
- **Item sets (customising) are not available** for an added character; it fights with the normal set.
- "Added characters in random picks" (F1 > Mods, off at first) lets the random cell and the tournament's computer
  entrants draw added characters.

Choose `base` as the disc character whose moves and build are nearest: the computer plays the added character
with the base's behaviour, and the base supplies whatever you leave out.

## 5. Replacing a character

A package with `replaces=<id>` takes the place of that disc character **in its own slot**: same cell, same id,
same place in every table, in every mode that uses the character (story fights included, since they ask for the
character by its id).

- **Any file the folder has is used in place of the disc's file of the same kind. Any file it does not have stays
  the disc's.** So a replacement can be as small as one file. Replacement files win over `gamedata/mods/`.
- `name`, `form` and `chip.bin` change the character's name, label and chip on the roster screens of section 8.
- `health`, `ki`, `blast` change those numbers for the slot (section 7).
- **It takes no added id and does not count toward the 57.** Up to all 161 characters can be replaced.
- **Two packages for one id:** the first by folder name is used, the other is not loaded and F1 > Mods says which
  one won.
- It needs `replaces=` and at least one thing to put in place (a file, a name, a label, a number, a link). The
  `*_1p.pak` that adding requires is not required here.
- **The number of costumes stays the disc character's.** A fourth costume cannot be added to a character that has
  three; a costume the folder does not replace is still on the select screen (see the next point for which model
  it shows).
- The forms of a replaced character are other disc characters with ids of their own. Replace each with a package
  of its own; it may sit beside the first or in a folder inside it (section 6).

**Whole character or skin.** The game tells the two apart by one thing: whether the folder has animations or
effects of its own.

| The folder ... | Missing damaged model of a costume it has | A costume it does not have at all |
|---|---|---|
| has **no** `*_anm.pak` and no `*_eff.pak` (a skin) | the folder's model of that costume | the disc's, untouched |
| has `*_anm.pak` or `*_eff.pak` (a whole character) | the folder's model of that costume | the folder's **first** costume |

The reason for the second row: a disc model on another character's animations and effects does not fit them, and
the game stops when a move asks for an effect that is not there. So a whole character with two costumes, put in
the place of a character with four, shows its first costume for colours 3 and 4.

The same care is yours to take for a partial package: **a model must fit the animations and effects it is used
with.** A recoloured or remodelled version of the same character fits. Another character's model alone, on the
slot's own animations and effects, does not.

One language only: unlike an added character, a replacement with only `*_voice_us.pak` leaves the Japanese bank
the disc's (and the other way round), by the rule "what the folder does not have stays the disc's".

## 6. Transformations and fusions

A character's own table (inside `*_1p.pak`) names its transformations and fusions by **character id**. For a
package made from another game or another mod, those numbers mean other characters here. The link keys say which
**folder** each one is.

| Key | What it sets |
|---|---|
| `left`, `up`, `right` | the three transformation inputs (R3 with a direction) |
| `down` | the way back to the earlier form |
| `fusion1`, `fusion2`, `fusion3` | the character each fusion makes |
| `partner1`, `partner2`, `partner3` | who must be on the team for that fusion, up to four, comma separated |

A link is one of:

| Written | Means |
|---|---|
| `Folder Name` | the package in that folder: looked for first among the folders inside the same character's folder, then among the folders directly in `characters` |
| `..` | from a folder inside a character's folder: that character |
| `Character/Form` | the folder `Form` inside the folder `Character` |
| `disc:<id>` | the disc character with that id |

- A link to a folder that **replaces** a disc character means that disc character.
- A key that is not written leaves the table's own number in force. For a replacement of a disc character made
  from that character's own files, that is what you want: write no links.
- A link to a folder that is not installed switches that transformation or fusion off, with a remark.
- **Folders inside a character's folder are its forms.** Each is a package of its own (an added character right
  after its owner, or a replacement if it says `replaces=`). What its manifest does not say about `base` and
  `name` is taken from the owner. One level only: folders inside a form's folder are not read. Up to 64 per
  character.
- A fusion's result has two parents: keep it as a folder directly in `characters` and link to it from both.
- On the roster, an added character that another added character transforms into (`left`, `up`, `right`) has no
  cell of its own; it is one of the forms under that character's cell. An added character that only a replaced
  disc character transforms into keeps a cell of its own.
- The fusion input is R3 held with left, up or right, in a team battle with the partner on the team.
- A link may be `none`: that transformation or fusion (or partner) is switched off, with no remark.

**The other numbers of a form.** What a transformation or a fusion costs and what it looks like are numbers of the
character's own table too. These keys set them; a key that is not written leaves the table's own (the Mod Manager's
FORMS screen writes them all for the part it changed):

| Key | Value | What it sets (the table's byte) |
|---|---|---|
| `left_cost`, `up_cost`, `right_cost`, `down_cost` | `0`..`9` | blast stocks it costs (+0x9C) |
| `left_type`, ... | `0`..`4` | 0 power up, 1 back to the earlier form, 2 great ape, 3 absorption, 4 machine merge (+0xA0) |
| `left_effect`, ... | `0`..`11` | the scene's effect; 3 needs the moon (+0xA4) |
| `left_assist`, ... | a disc character id, or `none` | the other fighter its scene shows (+0xA8) |
| `left_fresh`, ... | `0` or `1` | the new form starts with its clothes whole (bit 4..7 of +0xAD) |
| `default` | `left`, `up`, `right`, `down` or `none` | the form plain R3 (no direction) takes (+0xAC) |
| `fusion1_cost`, `fusion2_cost`, `fusion3_cost` | `0`..`9` | blast stocks the fusion costs (+0xAE) |
| `fusion1_type`, ... | `dance` or `potara` | which kind of fusion (+0xB1) |
| `fusion1_lead`, ... | a disc character id, or `none` | the partner its scene shows (+0xB7) |
| `fusion1_scene`, ... | `0`..`5` | the scene's motions as the Mod Manager names them; kept, not used by the fight yet |

They are put into the parameter table of the character's model when a fighter is set up from it (offline only, as
everything of a package). A disc character's forms set in the Mod Manager go into a package made for them,
`<the character's name> forms`, which replaces the character with nothing but its manifest.

## 7. Health, ki, blast stocks

`health=`, `ki=` and `blast=` set, for an added or a replaced character, the three numbers the game otherwise
takes from its table of characters:

| Key | Unit | The disc's range | Allowed |
|---|---|---|---|
| `health` | 10000 = one bar | 30000 .. 50000 | 10000 .. 150000 |
| `ki` | 100000 = a full gauge, at the start of a fight | 0 .. 100000 | 0 .. 100000 |
| `blast` | stocks | 4 .. 7 | 0 .. 7 |
| `damage` | percent of the damage the character's hits do | 100 | 10 .. 1000 |

`damage` scales every hit the character lands (rushes, blasts, ultimates); 100 leaves it as it is. Offline only,
as every number of a package. A character with `omni=1` keeps its own rule instead.

Items that add or take health work on top of the number, as they do in the original. The fight's display was
made for the disc's range; how it draws more than eight bars has not been looked at on a screen.

Everything else in the tables stays the slot's (replaced) or the base's (added): DP cost, AI type, level. Attack
strength, speed and the moves themselves are in the character's own `*_1p.pak` and come with it.

These keys make a character stronger or weaker than the original. That is the player's own game only (section 11).

## 8. Where a package shows

| Place | Added character | Replaced character |
|---|---|---|
| The fight: model, animations, effects, voice, spoken lines, the face on the display, the team mate's picture | its own | its own, wherever the disc character appears (every mode) |
| Character select of Duel and Team battle; the one-fighter and the team select of Ultimate Battle and Sim Dragon; the tournament's entry select and its bracket | its own cell, chip, name, label, portrait | its chip, name, label, portrait |
| The large picture on other screens (Ultimate Battle ranking, Sim Dragon's day screen, character reference) | its own on the first two | its own (it is the disc character's file) |
| Character reference (the encyclopedia) | not listed | listed as the disc character, with the **original** name and text; the model shown is the package's |
| Replay list, rankings' name lines, password screen | the base character's chip or name | the **original** chip and name |
| Names inside a fight (versus screen, pause menu, results) | not looked at | not looked at; expect the original where the game draws a name from its own text |
| Customising (Evolution Z) | not there | there as the disc character, original chip and name |
| Story modes | cannot be cast | fights use the package's files; names and pictures of the story screens are the original's |

## 9. Limits

| What | Limit |
|---|---|
| Added characters, forms included | 57 |
| Replaced characters | 161 (all) |
| Folders read directly in `characters` | 256, by name |
| Folders read inside one character's folder | 64 |
| Folder name | under 64 bytes, not starting with a dot |
| Path of a file (folder path + file name) | under 400 bytes; a longer one is not seen |
| Costumes | 4; for a replacement, the disc character's own number |
| File sizes | the table in section 2 |
| `name` on screen | 62 characters; the menus' font has the Latin letters and kana of the original game |

## 10. Not possible yet

- **Changing DP cost, AI behaviour, or anything else in the shared tables.** Only health, ki and blast stocks.
- **More costumes for a replaced character** than the disc character has.
- **A new character in the story modes**, or new story fights. A replaced character plays its disc character's
  part.
- **Text of the character reference**, and an entry there for an added character.
- **Password characters**: the password screen knows the disc's ids only.
- **Item sets for added characters.**
- **Names and chips on the screens marked "original" in section 8.**
- **Replays that survive a change of packages.** A replay stores the fighters' ids and the inputs, not the
  characters. One made with an added character needs the same packages in the same folder order to play back as
  recorded; one made with a replaced character needs that replacement. With anything else it still plays, but as
  a different fight.
- **Per-opponent lines before a fight** for an added character: it speaks as its base to the base's opponents.
- **A package read from inside an archive.** The game reads folders; the Mod Manager unpacks an archive into one.

## 11. Online play

**Online play does not know packages.** In an online session nothing of the `characters` folder is loaded, listed
or used, on either side: no added cell, no replaced file, no name, no chip, no number from a manifest. Both
players play the game's own characters, whatever each has installed, and two players need not have the same
packages, or any. When the session ends, the player's own game has its packages again. The data check that two
online players' games must pass does not look at the `characters` folder either.

This is deliberate and it is the rule of format 1. Do not build a package that expects to be used online.

(Files under `gamedata/mods/` are a different mechanism with rules of its own; see the main notes.)

## 12. Starting from the game's own characters

`port/tools/import_chara.py` copies a character out of your own game data (the unpacked `gamedata` folder, or a
disc image) into a package, with every file named as this page names it and a `character.txt` written:

```
python3 port/tools/import_chara.py gamedata --names                      the 161 ids and names
python3 port/tools/import_chara.py gamedata 55 "characters/My Fighter"   a new character standing on 55
python3 port/tools/import_chara.py gamedata 55 "characters/My Fighter" --replace
                                                                         the same files as a replacement of 55
python3 port/tools/import_chara.py gamedata 1 characters --with-forms    a character and its transformations,
                                                                         forms in folders inside, links written
python3 port/tools/import_chara.py gamedata 1 characters --with-forms --replace
                                                                         the same as replacements, each of its own id
```

`--replace` is the usual start for a mod of an existing character: the folder it makes changes nothing (every
file is the disc's own), so the game plays exactly as before; then edit the files you want to change and **delete
the ones you do not change**, so the package holds only your work.

A package made this way holds the game's own data. It is for your own use. Pass on only what you made.

### The pictures, from and to a PNG

`chip.bin`, `member_1p.bin`, `member_2p.bin` and `portrait.bin` are textures of the game: one byte a pixel and a
palette of 256 colours, in the order the console's video memory keeps them, with the commands that upload them;
`portrait.bin` is packed as well. The same tool turns them into PNG files and back, with no game data needed:

```
python3 import_chara.py --to-png portrait.bin portrait.png
python3 import_chara.py --from-png my_portrait.png portrait.bin --like portrait.bin
```

`--like` names any existing file of the same kind (the one an import wrote, or another package's): it gives the
kind and the size, and only its picture is replaced. It may be the file that is written.

| File | Size of the picture | Notes |
|---|---|---|
| `chip.bin` | 64 x 64 | the disc's chips keep the face in the middle and leave some rows at the top and the bottom empty: look at one with --to-png |
| `member_1p.bin`, `member_2p.bin` | 64 x 32 | the team mate's small picture, for the left and the right side |
| `portrait.bin` | 256 x 512 | stored squeezed: the menus show it about twice as wide, so draw it at 512 x 512 and scale the width down to 256 |

A PNG of another size is resized to the picture's (best made at the right size, or a whole multiple of it). A PNG
with more than 256 colours is brought down to 256. Transparency is kept: the console's alpha has 128 steps, so
half-transparent edges come out a little coarser. A packed portrait has to stay within 92,160 bytes; a picture
with a great deal of fine noise may not, and the tool then says so and writes nothing.

## 13. Testing a package

Start the game from a terminal and read the lines that begin `bt3: characters:`. At start it lists what was
found:

```
bt3: characters: 1 added from characters
bt3: characters:   My Fighter: id 198, 2 costumes, base character 55
bt3: characters: 1 of the disc's replaced from characters
bt3: characters:   Blue Gi: replaces character 0 (1 of its twelve files)
```

and every problem, by folder: a file too large, a link to a folder that is not there, a second package for one id.
F1 > Mods shows the same per package.

Environment variables for testing:

| Variable | Effect |
|---|---|
| `BT3_CHARACTERS=<folder>` | read packages from this folder instead of `characters` |
| `BT3_CHARA_LOG=1` | print, once for each, every file the game is given from a package (`file pzs3us1/01423.bin is characters/Blue Gi/x_1p.pak`), the names, labels and chips put on a select screen, and the cells made for added characters |
| `BT3_TEST_CHARA=<id>[,<id>]` | the first fighter of side 1 (and side 2) of every fight the menus set up is this character, whatever is chosen: `198` the first added one, `0`..`160` a disc character |
| `BT3_TEST_CELL=1` | the character select opens with the cursor on the first added character; `BT3_TEST_CELL=disc:<id>` on that disc character |

A check list before passing a package on:

1. The start-up lines name it, with the id or the character you meant, and no remark.
2. With `BT3_CHARA_LOG=1`, a fight shows your files being given for the model, the animations, the effects, the
   voice bank and, when lines are spoken, `voice_us/NNN.adx`.
3. Every costume on the select screen, and the damaged model (lose most of your health).
4. Every transformation and fusion you linked, both ways.
5. Both voice settings, if you ship both languages.
6. The package alone in `characters`, and together with others: its links must not depend on a folder you did
   not ship.

## 14. Examples

**A skin.** One costume of character 0 repainted. One model file and three lines:

```
characters/
  Blue Gi/
    character.txt        format=1
                         replaces=0
                         author=Somebody
    bluegi_1p.pak
```

Costume 1 of character 0 is the package's, also when its clothes tear; costumes 2 to 4, the animations, the voice
and everything else are the disc's.

**A whole replacement.** Character 55 becomes someone else, with a new name:

```
characters/
  New Champion/
    character.txt        format=1
                         replaces=55
                         name=New Champion
                         form=
                         health=40000
                         author=Somebody
                         version=2.0
                         about=Takes the place of character 55.
    champ_1p.pak   champ_1p_dmg.pak
    champ_2p.pak   champ_2p_dmg.pak
    champ_anm.pak  champ_eff.pak
    champ_voice_us.pak  champ_voice_jp.pak
    voice_us/000.adx ... 099.adx
    portrait.bin  chip.bin  member_1p.bin  member_2p.bin
```

**An added character with a transformation.** Two characters, the second reached with R3 + left and back with
R3 + down:

```
characters/
  Gokhan/
    character.txt        format=1
                         name=Gokhan
                         base=12
                         left=Super Saiyan
    gokhan_1p.pak  gokhan_anm.pak  gokhan_eff.pak  gokhan_voice_us.pak
    portrait.bin  chip.bin
    Super Saiyan/
      character.txt      format=1
                         form=Super Saiyan
                         base=13
                         down=..
      gokhan_ss_1p.pak  gokhan_ss_anm.pak  gokhan_ss_eff.pak
      portrait.bin  chip.bin
```

`Gokhan` gets id 198 and a cell; `Super Saiyan` gets 199 and is a form under that cell, with the name "Gokhan"
(taken from its owner) and its own label. Its voice bank is character 13's, since it has none.

## 15. Folders made before format 1

Folders made for earlier builds of this branch keep loading:

- `name.txt` (first line = `name`), `form.txt` (first line = `form`; an empty file = no label), `base.txt`
  (= `base`) and `forms.txt` (the link keys) are still read. Where `character.txt` says the same thing, it wins.
- **The two voice names changed meaning on 2026-10-13.** Before, `_voice_us` and `voice_us/` were taken for the
  disc's first set (which is the Japanese one). A folder made with the importer before that date has its two
  languages the wrong way round now: run the importer again, or exchange the names (`_voice_us.pak` with
  `_voice_jp.pak`, `voice_us` with `voice_jp`). Packages that come from the community's own tools were named this
  way all along and are right now.
