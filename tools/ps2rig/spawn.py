# Imported by make_arm.py for --spawn=x,y,z,ry: move the player Ravager's start.
# --spawn=district puts it back at the Motor District spawn (0, 0.5, -74.3035), yaw 0.
def apply(proj, edit, value):
    if value == 'district':
        x, y, z, ry = '0.0F', '0.5F', '-74.3035F', '0.0F'
        rot = '{0.0F, 0.0F, 0.0F}'
    else:
        a = value.split(',')
        x, y, z = (s + 'F' for s in a[:3])
        rot = '{0.0F, ' + a[3] + 'F, 0.0F}'
    p = proj / 'src/gen/scene_objects.gen.cpp'
    edit(p, '{5, {-119.314F, 0.316F, 0.294742F}, {-180.0F, 89.1092F, -180.0F}, {0.999986F, 1.0F, 0.999986F},',
         '{5, {' + x + ', ' + y + ', ' + z + '}, ' + rot + ', {1.0F, 1.0F, 1.0F},')
