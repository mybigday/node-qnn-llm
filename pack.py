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

try:
    from rich.progress import Progress, TaskID, SpinnerColumn, TextColumn, BarColumn, TimeRemainingColumn, MofNCompleteColumn
    from rich.console import Console
except ImportError:
    print("Error: rich module not found, please install it with `pip install rich`", file=sys.stderr)
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

def get_file_size(filepath):
    """Get file size in bytes"""
    return os.path.getsize(filepath)

def compress_with_progress(data, level, progress, task_id, description):
    """Compress data with progress updates"""
    progress.update(task_id, description=f"Compressing {description}")
    compressed = zstd.compress(data, level)
    progress.update(task_id, advance=1)
    return compressed

def pack_model(config_path, output_path, zstd_level=3):
    input_dir = os.path.dirname(config_path)
    console = Console()
    
    with Progress(
        SpinnerColumn(),
        TextColumn("[progress.description]{task.description}"),
        BarColumn(),
        MofNCompleteColumn(),
        TextColumn("[progress.percentage]{task.percentage:>3.0f}%"),
        TimeRemainingColumn(),
        console=console,
        transient=False,
    ) as progress:
        
        # Phase 1: Discovery and analysis
        discover_task = progress.add_task("🔍 Discovering files...", total=None)
        
        # 1) Read config first
        progress.update(discover_task, description="📖 Reading config file")
        raw_cfg = open(config_path, 'rb').read()
        config = json.loads(raw_cfg)
        
        # 2) Discover other files
        progress.update(discover_task, description="🔍 Discovering related files")
        config_tokenizer_path = config['dialog']['tokenizer']['path']
        
        # Convert to relative path
        if os.path.isabs(config_tokenizer_path):
            config_tokenizer_path = os.path.relpath(config_tokenizer_path, input_dir)
            if '..' in config_tokenizer_path:
                progress.update(discover_task, description="❌ Error: Invalid tokenizer path")
                console.print(f"[red]Error: tokenizer path is not relative to {input_dir}[/red]")
                sys.exit(1)
        
        if not os.path.exists(os.path.join(input_dir, config_tokenizer_path)):
            progress.update(discover_task, description="❌ Error: Tokenizer not found")
            console.print(f"[red]Error: tokenizer not found in {input_dir}[/red]")
            sys.exit(1)

        extra_entries = [config_tokenizer_path]

        if config['dialog']['engine']['model']['type'] == 'binary':
            ctx_bins = config['dialog']['engine']['model']['binary']['ctx-bins']
            for ctx_bin in ctx_bins:
                if not os.path.exists(os.path.join(input_dir, ctx_bin)):
                    progress.update(discover_task, description=f"❌ Error: Missing {ctx_bin}")
                    console.print(f"[red]Error: ctx-bin file \"{ctx_bin}\" not found in {input_dir}[/red]")
                    sys.exit(1)
            extra_entries.extend(ctx_bins)
        else:
            model_bin = config['dialog']['engine']['library']['model-bin']
            if not os.path.exists(os.path.join(input_dir, model_bin)):
                progress.update(discover_task, description=f"❌ Error: Missing {model_bin}")
                console.print(f"[red]Error: model-bin file \"{model_bin}\" not found in {input_dir}[/red]")
                sys.exit(1)
            extra_entries.append(model_bin)
        
        # Calculate total size for progress tracking
        total_raw_size = len(raw_cfg)
        file_sizes = {}
        for name in extra_entries:
            file_path = os.path.join(input_dir, name)
            size = get_file_size(file_path)
            file_sizes[name] = size
            total_raw_size += size
        
        progress.update(discover_task, description=f"✅ Found {len(extra_entries) + 1} files ({total_raw_size:,} bytes total)")
        progress.update(discover_task, completed=100)
        
        # Phase 2: Compression and packing
        total_files = len(extra_entries) + 1  # +1 for config
        pack_task = progress.add_task("📦 Packing files...", total=total_files)
        
        # 3) Open output and write placeholder header
        header_size = struct.calcsize(HEADER_FMT)
        with open(output_path, 'wb') as out:
            # placeholder header
            out.write(b'\x00' * header_size)

            # Compress and write config payload
            progress.update(pack_task, description="📝 Processing config.json")
            comp_cfg = compress_with_progress(raw_cfg, zstd_level, progress, pack_task, "config.json")
            crc_cfg = zlib.crc32(comp_cfg)
            
            cfg_offset = header_size
            out.write(comp_cfg)
            cfg_length = len(comp_cfg)

            # write tokenizer & model parts, record TOC entries
            toc = []
            cursor = cfg_offset + cfg_length
            
            for i, name in enumerate(extra_entries):
                file_path = os.path.join(input_dir, name)
                file_size = file_sizes[name]
                
                progress.update(pack_task, 
                              description=f"📄 Processing {name} ({file_size:,} bytes)")
                
                raw = open(file_path, 'rb').read()
                comp = compress_with_progress(raw, zstd_level, progress, pack_task, name)
                crc = zlib.crc32(comp)
                
                out.write(comp)
                toc.append({
                    'name': name.encode('utf-8'),
                    'offset': cursor,
                    'comp_length': len(comp),
                    'raw_length': len(raw),
                    'crc32': crc
                })
                cursor += len(comp)
                
                compression_ratio = (1 - len(comp) / len(raw)) * 100
                progress.update(pack_task, 
                              description=f"✅ {name} compressed ({compression_ratio:.1f}% reduction)")
        
        # Phase 3: Finalization
        finalize_task = progress.add_task("🔧 Finalizing container...", total=3)
        
        # 4) Write TOC
        with open(output_path, 'r+b') as out:
            out.seek(cursor)
            toc_offset = cursor
            
            progress.update(finalize_task, description="📋 Writing table of contents")
            for entry in toc:
                out.write(struct.pack('<H', len(entry['name'])))
                out.write(entry['name'])
                out.write(struct.pack('<Q Q Q I',
                                      entry['offset'],
                                      entry['comp_length'],
                                      entry['raw_length'],
                                      entry['crc32']))
            progress.advance(finalize_task)

            # 5) Backfill header
            progress.update(finalize_task, description="📄 Writing header")
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
            progress.advance(finalize_task)

        # 6) Append global CRC32
        progress.update(finalize_task, description="🔐 Computing global checksum")
        data = open(output_path, 'rb').read()
        global_crc = zlib.crc32(data)
        with open(output_path, 'ab') as out:
            out.write(struct.pack('<I', global_crc))
        progress.advance(finalize_task)
        
        # Calculate final statistics
        final_size = os.path.getsize(output_path)
        compression_ratio = (1 - final_size / total_raw_size) * 100
        
        console.print()
        console.print("🎉 [bold green]Packing completed successfully![/bold green]")
        console.print(f"📁 Input:  {total_raw_size:,} bytes ({len(extra_entries) + 1} files)")
        console.print(f"📦 Output: {final_size:,} bytes ({output_path})")
        console.print(f"📊 Compression: {compression_ratio:.1f}% reduction")
        console.print(f"📋 Sections: {len(toc)} + config")

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
