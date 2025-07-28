#!/usr/bin/env python3
import os
import sys
import struct
import zlib
import argparse
import json

try:
    import zstd
except ImportError:
    print("Error: zstd module not found, please install it with `pip install zstd`", file=sys.stderr)
    sys.exit(1)

# --------------------------------------------------------------------
# Container format:
#   Header: magic(7s), version(H), reserved(I),
#           config_offset(Q), config_length(Q), toc_offset(Q)
#   Payload:
#     [config_compressed]
#     [tokenizer_compressed]
#     [model_part_*.bin_compressed ...]
#   TOC entries (for tokenizer + model_parts only):
#     name_len (H), name (bytes),
#     offset (Q), comp_length (Q), raw_length (Q), crc32 (I)
#   Footer: global_crc32 (I)
# --------------------------------------------------------------------

MAGIC           = b'QGENIE1'         # 7-byte magic
VERSION         = 1                  # 2-byte version
HEADER_FMT      = '<7s H I Q Q Q'    # total size = 7+2+4 + 8+8+8 = 37 -> pad to 40 if you like

def pack_model(config_path, output_path, zstd_level=3):
    input_dir = os.path.dirname(config_path)

    # 1) Read & compress config first
    raw_cfg = open(config_path, 'rb').read()
    comp_cfg = zstd.compress(raw_cfg, zstd_level)
    crc_cfg  = zlib.crc32(comp_cfg)

    config = json.loads(raw_cfg)

    # 2) Discover other files
    config_tokenizer_path = config['dialog']['tokenizer']['path']
    # to relative path
    if os.path.isabs(config_tokenizer_path):
        config_tokenizer_path = os.path.relpath(config_tokenizer_path, input_dir)
        if '..' in config_tokenizer_path:
            sys.exit(f"Error: tokenizer path is not relative to {input_dir}")
    if not os.path.exists(os.path.join(input_dir, config_tokenizer_path)):
        sys.exit(f"Error: tokenizer not found in {input_dir}")

    extra_entries = [config_tokenizer_path]

    if config['dialog']['engine']['model']['type'] == 'binary':
        ctx_bins = config['dialog']['engine']['model']['binary']['ctx-bins']
        for ctx_bin in ctx_bins:
            if not os.path.exists(os.path.join(input_dir, ctx_bin)):
                sys.exit(f"Error: ctx-bin file \"{ctx_bin}\" not found in {input_dir}")
        extra_entries.extend(ctx_bins)
    else:
        model_bin = config['dialog']['engine']['library']['model-bin']
        if not os.path.exists(os.path.join(input_dir, model_bin)):
            sys.exit(f"Error: model-bin file \"{model_bin}\" not found in {input_dir}")
        extra_entries.append(model_bin)

    # 3) Open output and write placeholder header
    header_size = struct.calcsize(HEADER_FMT)
    with open(output_path, 'wb') as out:
        # placeholder header
        out.write(b'\x00' * header_size)

        # write config payload
        cfg_offset = header_size
        out.write(comp_cfg)
        cfg_length = len(comp_cfg)

        # write tokenizer & model parts, record TOC entries
        toc = []
        cursor = cfg_offset + cfg_length
        for name in extra_entries:
            raw = open(os.path.join(input_dir, name), 'rb').read()
            comp = zstd.compress(raw, zstd_level)
            crc  = zlib.crc32(comp)

            out.write(comp)
            toc.append({
                'name': name.encode('utf-8'),
                'offset': cursor,
                'comp_length': len(comp),
                'raw_length': len(raw),
                'crc32': crc
            })
            cursor += len(comp)

        # 4) Write TOC
        toc_offset = cursor
        for entry in toc:
            out.write(struct.pack('<H', len(entry['name'])))
            out.write(entry['name'])
            out.write(struct.pack('<Q Q Q I',
                                  entry['offset'],
                                  entry['comp_length'],
                                  entry['raw_length'],
                                  entry['crc32']))

        # 5) Backfill header
        out.seek(0)
        out.write(struct.pack(
            HEADER_FMT,
            MAGIC,
            VERSION,
            0,              # reserved
            cfg_offset,
            cfg_length,
            toc_offset
        ))
        out.flush()

    # 6) Append global CRC32
    data = open(output_path, 'rb').read()
    global_crc = zlib.crc32(data)
    with open(output_path, 'ab') as out:
        out.write(struct.pack('<I', global_crc))

    print(f"Packed config ({os.path.basename(config_path)}), "
          f"{len(toc)} sections into {output_path!r}")

if __name__ == '__main__':
    p = argparse.ArgumentParser(
        description="Bundle QNN Genie files into one compressed container"
    )
    p.add_argument('config_path',    help="Where to load config.json from")
    p.add_argument('-o', '--output', default='model.bin',
                   help="Output bundle file path")
    p.add_argument('-l', '--level',  type=int, default=3,
                   help="Zstd compression level (-7...22)")
    args = p.parse_args()

    pack_model(args.config_path, args.output, args.level)
