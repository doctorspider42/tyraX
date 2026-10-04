"""The Character Generator's wardrobe: which CC0 garment becomes which kit
item. Data only - kit_wear.py builds them (Blender), build_kit.py packs them
and the generator's Randomize reads `sex` to dress a man as a man.

(id, label, slot, kind, source, extra)
  slot  top / bottom / full / feet / head / face / hands / hair
  kind  'shell' (the body itself, pushed out and painted) or 'mesh'
  source 'sys:<dir>' = the MakeHuman system assets pack, 'packs:<pack>/<dir>'
        = one of the CC0 asset packs (fetch_sources.py downloads both)
  extra  style (mesh budget class), cutout (alpha-tested), sex ('f', 'm', '')
"""

CATALOG = [
    # ---- tops ----
    ('tee', 'T-shirt', 'top', 'shell', 'packs:shirts01/clothes/elvs_crude_t-shirt_male', {'sex': ''}),
    ('tee_fitted', 'Fitted T-shirt', 'top', 'shell', 'packs:shirts01/clothes/joepal_crude_t-shirt_female', {'sex': 'f'}),
    ('tee_tucked', 'Tucked T-shirt', 'top', 'shell', 'packs:shirts01/clothes/toigo_basic_tucked_t-shirt', {'sex': ''}),
    ('polo', 'Polo shirt', 'top', 'shell', 'packs:shirts01/clothes/namuhekam_male_polo_shirt', {'sex': 'm'}),
    ('sweater', 'Fisherman sweater', 'top', 'shell', 'packs:shirts01/clothes/toigo_fisherman_sweater', {'sex': ''}),
    ('tank', 'Keyhole tank top', 'top', 'shell', 'packs:shirts01/clothes/toigo_keyhole_tank_top', {'sex': 'f'}),
    ('halter', 'Turtleneck halter', 'top', 'shell', 'packs:shirts01/clothes/toigo_turtleneck_halter_top', {'sex': 'f'}),
    ('camisole', 'Camisole', 'top', 'shell', 'packs:shirts01/clothes/toigo_camisole_top', {'sex': 'f'}),
    ('bodice', 'Bodice top', 'top', 'shell', 'packs:shirts01/clothes/toigo_bodice-style_top', {'sex': 'f'}),
    # ---- bottoms ----
    ('trousers', 'Wool trousers', 'bottom', 'shell', 'packs:pants01/clothes/toigo_wool_pants', {'sex': ''}),
    ('cargo', 'Cargo pants', 'bottom', 'shell', 'packs:pants01/clothes/cortu_cargo_pants', {'sex': ''}),
    ('shorts', 'Jean shorts', 'bottom', 'shell', 'packs:pants01/clothes/cortu_jeans_shorts', {'sex': ''}),
    ('harem', 'Harem pants', 'bottom', 'shell', 'packs:pants01/clothes/toigo_harem_pants', {'sex': ''}),
    # ---- full outfits ----
    ('casual1', 'Casual outfit 1', 'full', 'shell', 'sys:clothes/male_casualsuit01', {'sex': 'm'}),
    ('casual2', 'Casual outfit 2', 'full', 'shell', 'sys:clothes/male_casualsuit02', {'sex': 'm'}),
    ('casual3', 'Casual outfit 3', 'full', 'shell', 'sys:clothes/male_casualsuit03', {'sex': 'm'}),
    ('casual4', 'Casual outfit 4', 'full', 'shell', 'sys:clothes/male_casualsuit04', {'sex': 'm'}),
    ('casual5', 'Casual outfit 5', 'full', 'shell', 'sys:clothes/male_casualsuit05', {'sex': 'm'}),
    ('casual6', 'Casual outfit 6', 'full', 'shell', 'sys:clothes/male_casualsuit06', {'sex': 'm'}),
    ('worksuit', 'Work overalls', 'full', 'shell', 'sys:clothes/male_worksuit01', {'sex': 'm'}),
    ('suit', 'Business suit', 'full', 'shell', 'sys:clothes/male_elegantsuit01', {'sex': 'm'}),
    ('fcasual1', 'Casual outfit 7', 'full', 'shell', 'sys:clothes/female_casualsuit01', {'sex': 'f'}),
    ('fcasual2', 'Casual outfit 8', 'full', 'shell', 'sys:clothes/female_casualsuit02', {'sex': 'f'}),
    ('felegant', 'Plaid shirt and shorts', 'full', 'shell', 'sys:clothes/female_elegantsuit01', {'sex': 'f'}),
    ('sport', 'Sportswear', 'full', 'shell', 'sys:clothes/female_sportsuit01', {'sex': 'f'}),
    ('dsuit', 'Double-breasted suit', 'full', 'shell', 'packs:suits01/clothes/toigo_male_double-breasted_suit', {'sex': 'm'}),
    ('tiesuit', 'Suit and tie', 'full', 'shell', 'packs:suits01/clothes/toigo_male_suit_tie_and_jacket', {'sex': 'm'}),
    ('fsuit', 'Trouser suit', 'full', 'shell', 'packs:suits01/clothes/toigo_female_suit_2', {'sex': 'f'}),
    # ---- skirts and dresses (meshes) ----
    ('mini', 'Mini skirt', 'bottom', 'mesh', 'packs:skirts01/clothes/frankyaye_mini_skirt_01', {'style': 'skirt', 'sex': 'f'}),
    ('mini2', 'Pleated mini skirt', 'bottom', 'mesh', 'packs:skirts01/clothes/frankyaye_mini_skirt_02', {'style': 'skirt', 'sex': 'f'}),
    ('longskirt', 'Long skirt', 'bottom', 'mesh', 'packs:skirts01/clothes/toigo_long_full_skirt', {'style': 'skirt', 'sex': 'f'}),
    ('tiered', 'Tiered mini skirt', 'bottom', 'mesh', 'packs:skirts01/clothes/toigo_tiered_mini_skirt', {'style': 'skirt', 'sex': 'f'}),
    ('halterdress', 'Halter dress', 'full', 'mesh', 'packs:dress01/clothes/toigo_halter_dress_knee_length', {'style': 'dress', 'sex': 'f'}),
    ('midi', 'Midi dress', 'full', 'mesh', 'packs:dress01/clothes/toigo_halter_dress_midi', {'style': 'dress', 'sex': 'f'}),
    ('shift', 'Shift dress', 'full', 'mesh', 'packs:dress01/clothes/toigo_shift_dress', {'style': 'dress', 'sex': 'f'}),
    ('cutoutdress', 'Cut-out dress', 'full', 'mesh', 'packs:dress01/clothes/toigo_cut_out_dress', {'style': 'dress', 'sex': 'f'}),
    ('flapper', 'Flapper dress', 'full', 'mesh', 'packs:dress01/clothes/aethelraed_flapper_dress', {'style': 'dress', 'sex': 'f'}),
    ('kimono', 'Kimono', 'full', 'mesh', 'packs:dress01/clothes/mindfront_kimono', {'style': 'dress', 'sex': 'f'}),
    ('tunic', 'Tunic', 'full', 'mesh', 'packs:dress01/clothes/wdg_mycenaean_tunic', {'style': 'dress', 'sex': ''}),
    # ---- shoes ----
    ('shoes1', 'Sneakers', 'feet', 'mesh', 'sys:clothes/shoes01', {'sex': ''}),
    ('shoes2', 'Loafers', 'feet', 'mesh', 'sys:clothes/shoes02', {'sex': ''}),
    ('shoes3', 'Boots', 'feet', 'mesh', 'sys:clothes/shoes03', {'sex': ''}),
    ('shoes4', 'Trainers', 'feet', 'mesh', 'sys:clothes/shoes04', {'sex': ''}),
    ('shoes5', 'Dress shoes', 'feet', 'mesh', 'sys:clothes/shoes05', {'sex': 'm'}),
    ('shoes6', 'Slip-ons with socks', 'feet', 'mesh', 'sys:clothes/shoes06', {'sex': 'f'}),
    ('ankleboots', 'Ankle boots', 'feet', 'mesh', 'packs:shoes01/clothes/toigo_ankle_boots_male', {'sex': 'm'}),
    ('fankleboots', 'Heeled ankle boots', 'feet', 'mesh', 'packs:shoes01/clothes/toigo_ankle_boots_female', {'sex': 'f'}),
    ('heroboots', 'Hero boots', 'feet', 'mesh', 'packs:shoes01/clothes/culturalibre_hero_boots_1', {'sex': ''}),
    ('canvas', 'Canvas shoes', 'feet', 'mesh', 'packs:shoes01/clothes/toigo_mj_cloth_shoes', {'sex': ''}),
    ('tbar', 'T-bar shoes', 'feet', 'mesh', 'packs:shoes01/clothes/cortu_t-bar', {'sex': 'f'}),
    ('flats', 'Ballet flats', 'feet', 'mesh', 'packs:shoes01/clothes/toigo_ballet_flats', {'sex': 'f'}),
    # ---- hats ----
    ('fedora', 'Fedora', 'head', 'mesh', 'sys:clothes/fedora01', {'sex': ''}),
    ('newsboy', 'Newsboy cap', 'head', 'mesh', 'packs:hats01/clothes/jujube_newsboy_cap', {'sex': ''}),
    ('cloche', 'Cloche hat', 'head', 'mesh', 'packs:hats01/clothes/aethelraed_unraed_cloche_hat', {'sex': 'f'}),
    ('santa', 'Santa hat', 'head', 'mesh', 'packs:hats01/clothes/joepal_xmas_cap', {'sex': ''}),
    ('tallhat', 'Tall hat', 'head', 'mesh', 'packs:hats01/clothes/grinsegold_uncle_joshis_hat', {'sex': ''}),
    # ---- glasses ----
    ('glasses1', 'Glasses', 'face', 'mesh', 'packs:glasses01/clothes/spamrakuen_sagerfrogs_glasses_01', {'sex': ''}),
    ('glasses2', 'Square frames', 'face', 'mesh', 'packs:glasses01/clothes/spamrakuen_tbm_glasses_frames_01', {'sex': ''}),
    ('glasses3', 'Library glasses', 'face', 'mesh', 'packs:glasses01/clothes/frankyaye_glasses_library_male', {'sex': ''}),
    ('shades', '3D glasses', 'face', 'mesh', 'packs:glasses01/clothes/ews_3d_glasses', {'sex': ''}),
    ('roundglasses', 'Round glasses', 'face', 'mesh', 'packs:glasses01/clothes/toigo_round_glasses_leopard', {'sex': ''}),
    # ---- hair ----
    ('short1', 'Short', 'hair', 'mesh', 'sys:hair/short01', {'cutout': True, 'sex': 'm'}),
    ('short2', 'Short side part', 'hair', 'mesh', 'sys:hair/short02', {'cutout': True, 'sex': 'm'}),
    ('short3', 'Pixie', 'hair', 'mesh', 'sys:hair/short03', {'cutout': True, 'sex': 'f'}),
    ('short4', 'Buzz', 'hair', 'mesh', 'sys:hair/short04', {'cutout': True, 'sex': 'm'}),
    ('afro', 'Afro', 'hair', 'mesh', 'sys:hair/afro01', {'cutout': True, 'sex': ''}),
    ('bob', 'Bob', 'hair', 'mesh', 'sys:hair/bob01', {'cutout': True, 'sex': 'f'}),
    ('bob2', 'Short bob', 'hair', 'mesh', 'sys:hair/bob02', {'cutout': True, 'sex': 'f'}),
    ('braid', 'Braid', 'hair', 'mesh', 'sys:hair/braid01', {'spring': 'hair', 'cutout': True, 'sex': 'f'}),
    ('long', 'Long', 'hair', 'mesh', 'sys:hair/long01', {'spring': 'hair', 'cutout': True, 'sex': 'f'}),
    ('ponytail', 'Ponytail', 'hair', 'mesh', 'sys:hair/ponytail01', {'spring': 'hair', 'cutout': True, 'sex': 'f'}),
    ('messy', 'Messy', 'hair', 'mesh', 'packs:hair01/hair/cortu_short_messy_hair', {'cutout': True, 'sex': 'm'}),
    ('bangs', 'Straight bangs', 'hair', 'mesh', 'packs:hair01/hair/cortu_straight_bangs', {'cutout': True, 'sex': 'f'}),
    ('bobcut', 'Bob cut', 'hair', 'mesh', 'packs:hair01/hair/littleright_bobcut_hair', {'cutout': True, 'sex': 'f'}),
    ('bluntbob', 'Blunt bob', 'hair', 'mesh', 'packs:hair01/hair/toigo_blunt_bob', {'cutout': True, 'sex': 'f'}),
    ('invbob', 'Inverted bob, bangs', 'hair', 'mesh', 'packs:hair01/hair/toigo_inverted_bob_with_bangs', {'cutout': True, 'sex': 'f'}),
    ('bun', 'Bun', 'hair', 'mesh', 'packs:hair01/hair/rehmanpolanski_hair_bun_brown', {'cutout': True, 'sex': 'f'}),
    ('wavy', 'Wavy', 'hair', 'mesh', 'packs:hair01/hair/faydaen_hair_1', {'spring': 'hair', 'cutout': True, 'sex': 'f'}),
    ('frenchbraid', 'French braid', 'hair', 'mesh', 'packs:hair01/hair/elvs_french_braid_variation', {'spring': 'hair', 'cutout': True, 'sex': 'f'}),
    ('headband', 'Headband', 'hair', 'mesh', 'packs:hair01/hair/sonntag78_blond_with_headband', {'spring': 'hair', 'cutout': True, 'sex': 'f'}),
    # ---- gloves ----
    ('gloves', 'Short gloves', 'hands', 'shell', 'packs:gloves01/clothes/toigo_gloves_short', {'sex': ''}),
    ('gloves_long', 'Long gloves', 'hands', 'shell', 'packs:gloves01/clothes/toigo_gloves_long', {'sex': 'f'}),
    ('mma', 'Fighting gloves', 'hands', 'shell', 'packs:gloves01/clothes/learning_mma_fighting_gloves', {'sex': ''}),
]

