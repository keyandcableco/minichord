# Room for the user drum kit (drum kit 13, include/user_kit.h and generator/user_kit.md): its sound lives in
# the flash from data_base, the first 64 kB boundary after the firmware's image (0x601A0000 if that's
# lower), up to 0x601E0000, where its header sector is, with the presets above it at 0x601E8000. So every
# kB the firmware grows past 0x601A0000 is a kB of kit lost, and a stored kit stops checking when data_base
# moves (the player sends it again). After each link this prints where the image ends and what that
# leaves, warns when it's less than the full 256 kB, and fails the build when it's under 64 kB (the
# minichord would have no kit slot at all) or the image reaches the header.
#
# The image's length is the linker's _flashimagelen (the Teensy core's linker script: everything up to the
# end of the code signing data), read from the ELF's symbols with the toolchain's nm. Any failure, a check
# or not being able to read the symbol at all, deletes the ELF, so the next build links again and checks
# again rather than taking the old one as done.
import os
import subprocess
Import("env")

FLASH = 0x60000000
KIT_HEADER = 0x601E0000
KIT_TOP = 0x601E0000
KIT_FLOOR = 0x601A0000
FULL = KIT_TOP - KIT_FLOOR      # 262,144
MINIMUM = 65536


# nm beside the compiler: only the program's name changes (arm-none-eabi-gcc to arm-none-eabi-nm), never a
# "gcc" in the directory it's in, should $CC ever be a full path
def nm_path(env):
    cc = env.subst("$CC")
    folder, name = os.path.split(cc)
    if not name.endswith("gcc"):
        raise RuntimeError("the compiler %r isn't a gcc, so there's no nm to put beside it" % cc)
    return os.path.join(folder, name[:-3] + "nm")


def image_length(elf, env):
    out = subprocess.run([nm_path(env), elf], capture_output=True, text=True, env=env["ENV"], check=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == "_flashimagelen":
            return int(parts[0], 16)
    raise RuntimeError("no _flashimagelen in %s" % elf)


def failed(elf, problem):
    print("flash_room: ERROR: " + problem)
    if os.path.exists(elf):
        os.remove(elf)
    return 1


def flash_room(target, source, env):
    elf = str(target[0])
    try:
        end = FLASH + image_length(elf, env)
    except Exception as e:   # nm missing or failing, the symbol not there: all of them a failed check
        return failed(elf, "can't read the image's length (%s)" % e)
    data_base = max(KIT_FLOOR, (end + 65535) & ~65535)
    capacity = max(0, KIT_TOP - data_base)
    print("flash_room: image ends at 0x%08X, user kit from 0x%08X, %d bytes (%d kB) of room" % (end, data_base, capacity, capacity // 1024))
    if end > KIT_HEADER:
        return failed(elf, "the firmware image reaches the user kit's header at 0x%08X" % KIT_HEADER)
    if capacity < MINIMUM:
        return failed(elf, "under 64 kB left for the user kit: the minichord would have no kit slot")
    if capacity < FULL:
        print("flash_room: WARNING: the user kit has %d bytes, less than the full %d: the firmware has grown past 0x%08X" % (capacity, FULL, KIT_FLOOR))
    return 0


env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", flash_room)
