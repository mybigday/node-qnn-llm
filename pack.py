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

class ModelPacker:
    """Class to handle packing of QNN Genie model configurations"""
    
    def __init__(self, zstd_level=3):
        self.zstd_level = zstd_level
        self.console = Console()
    
    def get_file_size(self, filepath):
        """Get file size in bytes"""
        return os.path.getsize(filepath)
    
    def format_size(self, size_bytes):
        """Convert bytes to human readable format (KB, MB, GB, etc.)"""
        if size_bytes == 0:
            return "0 B"
        
        size_names = ["B", "KB", "MB", "GB", "TB", "PB"]
        i = 0
        size = float(size_bytes)
        
        while size >= 1024.0 and i < len(size_names) - 1:
            size /= 1024.0
            i += 1
        
        if i == 0:
            return f"{int(size)} {size_names[i]}"
        else:
            return f"{size:.1f} {size_names[i]}"
    
    def compress_with_progress(self, data, level, progress, task_id, description):
        """Compress data with progress updates"""
        progress.update(task_id, description=f"Compressing {description}")
        compressed = zstd.compress(data, level)
        progress.update(task_id, advance=1)
        return compressed
    
    def discover_files(self, config, input_dir, progress, console):
        """Discover and validate files needed for packing"""
        discover_task = progress.add_task("🔍 Discovering files...", total=None)
        
        # Determine config type and get tokenizer path
        if 'dialog' in config:
            config_type = 'dialog'
            tokenizer_path = config['dialog']['tokenizer']['path']
            engine_config = config['dialog']['engine']
        elif 'embedding' in config:
            config_type = 'embedding'
            tokenizer_path = config['embedding']['tokenizer']['path']
            engine_config = config['embedding']['engine']
        else:
            progress.update(discover_task, description="❌ Error: Unknown config type")
            console.print("[red]Error: Config must contain either 'dialog' or 'embedding' section[/red]")
            sys.exit(1)
        
        progress.update(discover_task, description="🔍 Discovering related files")
        
        # Convert to relative path
        if os.path.isabs(tokenizer_path):
            tokenizer_path = os.path.relpath(tokenizer_path, input_dir)
            if '..' in tokenizer_path:
                progress.update(discover_task, description="❌ Error: Invalid tokenizer path")
                console.print(f"[red]Error: tokenizer path is not relative to {input_dir}[/red]")
                sys.exit(1)
        
        if not os.path.exists(os.path.join(input_dir, tokenizer_path)):
            progress.update(discover_task, description="❌ Error: Tokenizer not found")
            console.print(f"[red]Error: tokenizer not found in {input_dir}[/red]")
            sys.exit(1)

        extra_entries = [tokenizer_path]

        # Add htp ext config file if it exists
        if engine_config['model']['backend']['type'] == 'QnnHtp':
            extensions = engine_config['model']['backend']['QnnHtp']['extensions']
            if os.path.exists(os.path.join(input_dir, extensions)):
                extra_entries.append(extensions)

        # Handle different model types
        if engine_config['model']['type'] == 'binary':
            ctx_bins = engine_config['model']['binary']['ctx-bins']
            for ctx_bin in ctx_bins:
                if not os.path.exists(os.path.join(input_dir, ctx_bin)):
                    progress.update(discover_task, description=f"❌ Error: Missing {ctx_bin}")
                    console.print(f"[red]Error: ctx-bin file \"{ctx_bin}\" not found in {input_dir}[/red]")
                    sys.exit(1)
            extra_entries.extend(ctx_bins)
        else:
            model_bin = engine_config['model']['library']['model-bin']
            if not os.path.exists(os.path.join(input_dir, model_bin)):
                progress.update(discover_task, description=f"❌ Error: Missing {model_bin}")
                console.print(f"[red]Error: model-bin file \"{model_bin}\" not found in {input_dir}[/red]")
                sys.exit(1)
            extra_entries.append(model_bin)
        
        return extra_entries
    
    def calculate_sizes(self, extra_entries, input_dir, raw_cfg):
        """Calculate total sizes for progress tracking"""
        total_raw_size = len(raw_cfg)
        file_sizes = {}
        for name in extra_entries:
            file_path = os.path.join(input_dir, name)
            size = self.get_file_size(file_path)
            file_sizes[name] = size
            total_raw_size += size
        return total_raw_size, file_sizes
    
    def write_container(self, raw_cfg, extra_entries, file_sizes, input_dir, output_path, progress):
        """Write the compressed container with TOC"""
        # Phase 2: Compression and packing
        total_files = len(extra_entries) + 1  # +1 for config
        pack_task = progress.add_task("📦 Packing files...", total=total_files)
        
        # Open output and write placeholder header
        header_size = struct.calcsize(HEADER_FMT)
        with open(output_path, 'wb') as out:
            # placeholder header
            out.write(b'\x00' * header_size)

            # Compress and write config payload
            progress.update(pack_task, description="📝 Processing config.json")
            comp_cfg = self.compress_with_progress(raw_cfg, self.zstd_level, progress, pack_task, "config.json")
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
                              description=f"📄 Processing {name} ({self.format_size(file_size)})")
                
                raw = open(file_path, 'rb').read()
                comp = self.compress_with_progress(raw, self.zstd_level, progress, pack_task, name)
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
        
        return toc, cfg_offset, cfg_length, cursor
    
    def finalize_container(self, output_path, toc, cfg_offset, cfg_length, cursor, progress):
        """Finalize the container with TOC, header, and CRC"""
        # Phase 3: Finalization
        finalize_task = progress.add_task("🔧 Finalizing container...", total=3)
        
        # Write TOC
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

            # Backfill header
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

        # Append global CRC32
        progress.update(finalize_task, description="🔐 Computing global checksum")
        data = open(output_path, 'rb').read()
        global_crc = zlib.crc32(data)
        with open(output_path, 'ab') as out:
            out.write(struct.pack('<I', global_crc))
        progress.advance(finalize_task)
    
    def print_statistics(self, total_raw_size, output_path, extra_entries, toc):
        """Print final packing statistics"""
        final_size = os.path.getsize(output_path)
        compression_ratio = (1 - final_size / total_raw_size) * 100
        
        self.console.print()
        self.console.print("🎉 [bold green]Packing completed successfully![/bold green]")
        self.console.print(f"📁 Input:  {self.format_size(total_raw_size)} ({len(extra_entries) + 1} files)")
        self.console.print(f"📦 Output: {self.format_size(final_size)} ({output_path})")
        self.console.print(f"📊 Compression: {compression_ratio:.1f}% reduction")
        self.console.print(f"📋 Sections: {len(toc)} + config")
    
    def pack_model(self, config_path, output_path):
        """Main packing method that handles both dialog and embedding configs"""
        input_dir = os.path.dirname(config_path)
        
        with Progress(
            SpinnerColumn(),
            TextColumn("[progress.description]{task.description}"),
            BarColumn(),
            MofNCompleteColumn(),
            TextColumn("[progress.percentage]{task.percentage:>3.0f}%"),
            TimeRemainingColumn(),
            console=self.console,
            transient=False,
        ) as progress:
            
            # Phase 1: Discovery and analysis
            discover_task = progress.add_task("🔍 Discovering files...", total=None)
            
            # Read config first
            progress.update(discover_task, description="📖 Reading config file")
            raw_cfg = open(config_path, 'rb').read()
            config = json.loads(raw_cfg)
            
            # Discover files
            extra_entries = self.discover_files(config, input_dir, progress, self.console)
            
            # Calculate sizes
            total_raw_size, file_sizes = self.calculate_sizes(extra_entries, input_dir, raw_cfg)
            
            progress.update(discover_task, description=f"✅ Found {len(extra_entries) + 1} files ({self.format_size(total_raw_size)} total)")
            progress.update(discover_task, completed=100)
            
            # Write container
            toc, cfg_offset, cfg_length, cursor = self.write_container(
                raw_cfg, extra_entries, file_sizes, input_dir, output_path, progress
            )
            
            # Finalize container
            self.finalize_container(output_path, toc, cfg_offset, cfg_length, cursor, progress)
            
            # Print statistics
            self.print_statistics(total_raw_size, output_path, extra_entries, toc)

def pack_model(config_path, output_path, zstd_level=3):
    """Legacy function wrapper for backward compatibility"""
    packer = ModelPacker(zstd_level)
    packer.pack_model(config_path, output_path)

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

    # Use the new class-based approach
    packer = ModelPacker(args.level)
    packer.pack_model(args.config_path, args.output)
