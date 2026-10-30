# PlatformIO extra script for ak-mcu-base (STM32L151).
#  - link flags: no startup files, newlib-nano, search path for sections.ld
#  - env:app post-build: patch image header (size + CRC) into firmware.elf and
#    write app.img next to it (same steps as src/port/stm32l151/port.cmake)
#  - the SPL lives outside src_dir (third_party/stm32l1): built from here
Import("env")

import os

proj = env.subst("$PROJECT_DIR")

cfg = env.GetProjectConfig()
staging_external = cfg.get("common", "staging_external", "1").strip() == "1"
image_sign = cfg.get("common", "image_sign", "0").strip() == "1"
sign_key = cfg.get("common", "sign_key", "tools/keys/dev_priv.pem").strip()
# APP RAM (port_cfg.h): 16K runs on L151CB and CB-A, 32K only on CB-A
ram_size = {"16": "0x4000", "32": "0x8000"}.get(cfg.get("common", "ram_size_kb", "16").strip())
if ram_size is None:
    raise SystemExit("[common] ram_size_kb must be 16 or 32")
# signed images: 16K bootloader, APP 4K smaller (port_cfg.h)
if image_sign and not os.path.exists(os.path.join(proj, sign_key)):
    raise SystemExit("No signing key at %s. Make a development pair first: python tools/ak_sign.py devkey" % sign_key)
boot_size = "0x4000" if image_sign else "0x3000"
if staging_external:
    app_part_size = "0x1C000" if image_sign else "0x1D000"
else:
    app_part_size = "0xE000" if image_sign else "0xE800"

env.Append(LINKFLAGS=[
    "-mcpu=cortex-m3", "-mthumb", "-mfloat-abi=soft", "-Os", "-flto",
    "-Wl,--defsym=__app_part_size=" + app_part_size,
    "-Wl,--defsym=__boot_size=" + boot_size,
    "-Wl,--defsym=__ram_size=" + ram_size,
    "-nostartfiles", "--specs=nano.specs",
    "-Wl,--gc-sections", "-Wl,--print-memory-usage",
    "-L" + os.path.join(proj, "src", "port", "stm32l151"),
])

# SPL sources (outside src_dir -> built explicitly). Location: [common] spl in
# platformio.ini.
spl_dir = env.GetProjectConfig().get("common", "spl").strip()
spl_src = os.path.normpath(os.path.join(proj, spl_dir, "STM32L1xx_StdPeriph_Driver", "src"))
if not os.path.isdir(spl_src):
    raise SystemExit("SPL not found: %s (check [common] spl in platformio.ini)" % spl_src)
env.BuildSources(os.path.join("$BUILD_DIR", "spl"), spl_src, src_filter=[
    "-<*>", "+<misc.c>", "+<stm32l1xx_rcc.c>", "+<stm32l1xx_gpio.c>",
    "+<stm32l1xx_usart.c>", "+<stm32l1xx_flash.c>", "+<stm32l1xx_iwdg.c>",
    "+<stm32l1xx_spi.c>",
])

# ECDSA verify (third_party/p256-m), only with signed images
if image_sign:
    env.BuildSources(os.path.join("$BUILD_DIR", "p256m"), os.path.join(proj, "third_party", "p256-m"),
                     src_filter=["+<p256-m.c>"])

if env.subst("$PIOENV") != "boot":
    def patch_image(source, target, env):
        elf = target[0].get_abspath()
        build = env.subst("$BUILD_DIR")
        raw = os.path.join(build, "app_raw.bin")
        img = os.path.join(build, "app.img")
        objcopy = env.subst("$OBJCOPY")
        env.Execute('"%s" -O binary "%s" "%s"' % (objcopy, elf, raw))
        sign = ' --sign-key "%s"' % os.path.join(proj, sign_key) if image_sign else ""
        env.Execute('"$PYTHONEXE" "%s" patch "%s" -o "%s" --elf "%s" --objcopy "%s"%s' % (
            os.path.join(proj, "tools", "mkimage.py"), raw, img, elf, objcopy, sign))

    env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", patch_image)
