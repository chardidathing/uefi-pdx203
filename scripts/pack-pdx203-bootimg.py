#!/usr/bin/env python3
"""pack the pdx203 firmware volume into an android boot image for boot_a, the way the stock sony image is laid out.

kernel field = gzip(BootShim + SILICIUM_UEFI.fd), header v2 with the stock kona dtb set in the dtb section and the stock
offsets (Resources/Configs/pdx203.toml), the ramdisk is the unixnu payload volume when given, else an empty cpio (the loader
has been seen to dislike a zero length ramdisk). the image is unpacked again and every part compared with its input.

the unixnu driver (unixnu bootimg) appends the dtb to the gzip stream instead, a header v1 container this bootloader has not
been seen to accept, which is why this script exists. same shim compile and gzip as the driver, so the kernel bytes match.
"""
import argparse
import gzip
import hashlib
import json
import shutil
import struct
import subprocess
import sys
import tempfile
import tomllib
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / 'Resources/Configs/pdx203.toml'
MKBOOTIMG = ROOT / 'Resources/Scripts/mkbootimg.py'
SHIM = ROOT / 'BootShim/BootShim.S'
FLAGS = ('base', 'kernel_offset', 'ramdisk_offset', 'second_offset', 'tags_offset', 'dtb_offset', 'pagesize',
         'header_version', 'os_version', 'os_patch_level', 'cmdline')


def tool(name):
    found = shutil.which(name)
    if not found:
        sys.exit(f'{name} not found on PATH')
    return found


def run(command, **kwargs):
    result = subprocess.run([str(part) for part in command], capture_output=True, text=True, **kwargs)
    if result.returncode:
        sys.exit(f'{command[0]} failed ({result.returncode}): {result.stderr.strip() or result.stdout.strip()}')
    return result.stdout


def compile_shim(base, size, scratch):
    obj = scratch / 'shim.o'
    run([tool('clang'), '--target=aarch64-none-elf', '-c', f'-DFD_BASE={base:#x}', f'-DFD_SIZE={size:#x}',
         '-DREQUIRES_KERNEL_HEADER=1', SHIM, '-o', obj])
    run([tool('llvm-objcopy'), '-O', 'binary', obj, scratch / 'shim.bin'])
    shim = (scratch / 'shim.bin').read_bytes()
    symbols = run([tool('llvm-nm'), '-n', obj])
    payload = next((int(line.split()[0], 16) for line in symbols.splitlines() if line.endswith(' _Payload')), None)
    if payload != len(shim):
        sys.exit('the _Payload label is not at the end of the shim binary')
    if shim[56:60] != b'ARM\x64' or struct.unpack_from('<QQ', shim, 8) != (base, len(shim) + size):
        sys.exit('the arm64 image header does not describe the shim plus the firmware volume')
    return shim


def empty_cpio():
    """a newc archive with only the trailer, gzipped like the ios-sweet shim's diagnostics ramdisk."""
    name = b'TRAILER!!!\0'
    header = b'070701' + b'%08x' * 13 % (0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, len(name), 0)
    body = header + name
    body += b'\0' * (-len(body) % 4)
    return gzip.compress(body, mtime=0)


def unpack(data):
    """parts of a header v2 image, (offset, size) each, from the android boot image header."""
    if data[:8] != b'ANDROID!':
        sys.exit('not an android boot image')
    kernel, _, ramdisk, _, second, _, _, page, version, _ = struct.unpack_from('<10I', data, 8)
    if version != 2:
        sys.exit(f'header version {version}, expected 2')
    recovery_dtbo = struct.unpack_from('<I', data, 1632)[0]
    dtb, dtb_addr = struct.unpack_from('<IQ', data, 1648)
    parts = {}
    offset = page
    for name, size in (('kernel', kernel), ('ramdisk', ramdisk), ('second', second), ('recovery_dtbo', recovery_dtbo), ('dtb', dtb)):
        parts[name] = (offset, size)
        offset += -(-size // page) * page
    return parts, page, dtb_addr


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fd', help='firmware volume (default: the DEBUG build output)')
    parser.add_argument('--target', default='DEBUG', choices=('DEBUG', 'RELEASE'))
    parser.add_argument('--ramdisk', help='ramdisk image, the unixnu payload volume (default: an empty cpio)')
    parser.add_argument('--dtb', help='dtb set for the dtb section (default: the stock set of the config)')
    parser.add_argument('-o', '--output', required=True, help='boot image to write')
    args = parser.parse_args()

    config = tomllib.loads(CONFIG.read_text())
    base, size = config['uefi_fd']['base'], config['uefi_fd']['size']
    image = config['boot_image']
    fd = Path(args.fd) if args.fd else ROOT / f'Build/pdx203Pkg/{args.target}_CLANGPDB/FV/SILICIUM_UEFI.fd'
    dtb = Path(args.dtb) if args.dtb else ROOT / image['dtb']
    for path in (fd, dtb, MKBOOTIMG):
        if not path.is_file():
            sys.exit(f'missing: {path}')
    firmware = fd.read_bytes()
    if len(firmware) != size:
        sys.exit(f'firmware volume is {len(firmware):#x} bytes, the config says {size:#x}')
    tree = dtb.read_bytes()
    if tree[:4] != b'\xd0\x0d\xfe\xed':
        sys.exit('the dtb set does not start with a dtb')
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix='pdx203-bootimg-') as name:
        scratch = Path(name)
        shim = compile_shim(base, size, scratch)
        combined = shim + firmware
        kernel = scratch / 'kernel.gz'
        kernel.write_bytes(gzip.compress(combined, mtime=0))
        ramdisk = Path(args.ramdisk) if args.ramdisk else scratch / 'ramdisk.cpio.gz'
        if not args.ramdisk:
            ramdisk.write_bytes(empty_cpio())
        if not ramdisk.is_file():
            sys.exit(f'missing: {ramdisk}')
        command = ['python3', MKBOOTIMG, '--kernel', kernel, '--ramdisk', ramdisk, '--dtb', dtb, '-o', output]
        for key in FLAGS:
            if key in image:
                command += [f'--{key}', str(image[key])]
        run(command)

    data = output.read_bytes()
    parts, page, dtb_addr = unpack(data)
    if page != image['pagesize']:
        sys.exit('page size differs from the config')
    stream = data[parts['kernel'][0]:parts['kernel'][0] + parts['kernel'][1]]
    inflater = zlib.decompressobj(16 + zlib.MAX_WBITS)
    if inflater.decompress(stream) != combined or not inflater.eof or inflater.unused_data:
        sys.exit('the kernel field does not round-trip to shim + firmware volume')
    if data[parts['ramdisk'][0]:parts['ramdisk'][0] + parts['ramdisk'][1]] != ramdisk.read_bytes() if args.ramdisk else parts['ramdisk'][1] != len(empty_cpio()):
        sys.exit('ramdisk bytes differ')
    if data[parts['dtb'][0]:parts['dtb'][0] + parts['dtb'][1]] != tree:
        sys.exit('dtb section bytes differ')
    if dtb_addr != image['base'] + image['dtb_offset']:
        sys.exit('dtb load address differs from the config')

    digest = hashlib.sha256(data).hexdigest()
    report = {'image': str(output), 'image_bytes': len(data), 'image_sha256': digest,
              'fd': str(fd), 'fd_sha256': hashlib.sha256(firmware).hexdigest(), 'fd_base': hex(base), 'fd_size': hex(size),
              'shim_bytes': len(shim), 'kernel_gzip_bytes': parts['kernel'][1],
              'ramdisk': str(ramdisk) if args.ramdisk else 'empty cpio', 'ramdisk_bytes': parts['ramdisk'][1],
              'dtb': str(dtb), 'dtb_sha256': hashlib.sha256(tree).hexdigest(), 'dtb_bytes': parts['dtb'][1],
              'header_version': 2, 'pagesize': page, 'hardware_tested': False}
    output.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
    output.with_suffix('.img.sha256').write_text(f'{digest}  {output.name}\n')
    print(f'{output} ({len(data)} bytes) sha256 {digest}')


if __name__ == '__main__':
    main()
