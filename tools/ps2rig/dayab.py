# Imported by make_arm.py for --daynightab: arm 1 runs the district at day
# (lamps off, night-only objects hidden), arms 0 and 2 at night.
def apply(proj, edit, include):
    p = proj / 'src/scripts/district_mood.cpp'
    include(p)
    edit(p, '    const bool night = true;  // Claude night rig',
         '    const bool night = Cld::arm != 1;  // Claude rig: arm 1 = day')
