#!/usr/bin/env python3
"""
build_logos_local.py — FlightWall local airline logo builder
============================================================
Reads manually downloaded logo images from a local folder, converts them to
RGB565 binary blobs, and writes them to firmware/data/logos/{ICAO}.bin
ready for upload to the ESP32 via LittleFS.

Usage:
    pip install Pillow
    python tools/build_logos_local.py --in-dir ./my_downloaded_logos [--width W] [--height H] [--out OUT_DIR]

Naming:
    Name your source image files after the airline's ICAO code (e.g., AAL.png, DAL.jpg).
    The script ignores extensions and casing, matching the prefix to the AIRLINE_LIST.
"""

import argparse
import struct
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("ERROR: Missing dependencies. Run: pip install Pillow")
    sys.exit(1)

# ─── Configurable defaults ────────────────────────────────────────────────────
DEFAULT_WIDTH  = 32
DEFAULT_HEIGHT = 32
SCRIPT_DIR     = Path(__file__).parent
DEFAULT_IN     = SCRIPT_DIR.parent / "logos_input"
DEFAULT_OUT    = SCRIPT_DIR.parent / "firmware" / "data" / "logos"

# Transparent colour: pure magenta in RGB565
TRANSPARENT_RGB565 = 0xF81F

# ─── Airline list ─────────────────────────────────────────────────────────────
AIRLINE_LIST = [
    ("AAL", "AA", "American Airlines"),
    ("DAL", "DL", "Delta Air Lines"),
    ("UAL", "UA", "United Airlines"),
    ("SWA", "WN", "Southwest Airlines"),
    ("ASA", "AS", "Alaska Airlines"),
    ("JBU", "B6", "JetBlue Airways"),
    ("SKW", "OO", "SkyWest Airlines"),
    ("FFT", "F9", "Frontier Airlines"),
    ("NKS", "NK", "Spirit Airlines"),
    ("HAL", "HA", "Hawaiian Airlines"),
    ("SUN", "SY", "Sun Country Airlines"),
    ("GTI", "GS", "Atlas Air"),
    ("ABX", "GB", "ABX Air"),
    ("FDX", "FX", "FedEx Express"),
    ("UPS", "5X", "UPS Airlines"),

    # ── Canada ────────────────────────────────────────────────────────────────
    ("ACA", "AC", "Air Canada"),
    ("WJA", "WS", "WestJet"),
    ("TTS", "TS", "Air Transat"),

    # ── Europe ────────────────────────────────────────────────────────────────
    ("RYR", "FR", "Ryanair"),
    ("EZY", "U2", "easyJet"),
    ("DLH", "LH", "Lufthansa"),
    ("BAW", "BA", "British Airways"),
    ("AFR", "AF", "Air France"),
    ("KLM", "KL", "KLM Royal Dutch"),
    ("IBE", "IB", "Iberia"),
    ("VLG", "VY", "Vueling"),
    ("NAX", "DY", "Norwegian"),
    ("SAS", "SK", "Scandinavian Airlines"),
    ("AZA", "AZ", "ITA Airways"),
    ("TAP", "TP", "TAP Air Portugal"),
    ("AUA", "OS", "Austrian Airlines"),
    ("SWR", "LX", "Swiss International"),
    ("BEL", "SN", "Brussels Airlines"),
    ("THY", "TK", "Turkish Airlines"),
    ("WZZ", "W6", "Wizz Air"),
    ("TOM", "BY", "TUI Airways"),
    ("AEE", "A3", "Aegean Airlines"),
    ("LOT", "LO", "LOT Polish Airlines"),
    ("CSA", "OK", "Czech Airlines"),
    ("FIN", "AY", "Finnair"),
    ("ICE", "FI", "Icelandair"),
    ("UAE", "EK", "Emirates"),
    ("ETD", "EY", "Etihad Airways"),
    ("QTR", "QR", "Qatar Airways"),
    ("EXS", "LS", "Jet2.com"),
    ("EIN", "EI", "Aer Lingus"),
    ("EZS", "DS", "easyJet"),
    ("EJU", "EC", "easyJet"),
    ("RYS", "RR", "Buzz"),
    ("CFE", "CF", "BA CityFlyer"),
    ("SXS", "XQ", "SunExpress"),
    ("EWE", "E2", "Eurowings Europe"),
    ("EWG", "EW", "Eurowings"),
    ("NOZ", "DY", "Norwegian Air Shuttle"),
    ("NBT", "N0", "Norse Atlantic Airways"),

    # ── Asia-Pacific ─────────────────────────────────────────────────────────
    ("CCA", "CA", "Air China"),
    ("CSN", "CZ", "China Southern"),
    ("CES", "MU", "China Eastern"),
    ("CHH", "HU", "Hainan Airlines"),
    ("XAM", "MF", "Xiamen Airlines"),
    ("SHQ", "SC", "Shandong Airlines"),
    ("CXA", "KN", "China United Airlines"),
    ("JAL", "JL", "Japan Airlines"),
    ("ANA", "NH", "All Nippon Airways"),
    ("JJP", "GK", "Jetstar Japan"),
    ("KAL", "KE", "Korean Air"),
    ("AAR", "OZ", "Asiana Airlines"),
    ("SIA", "SQ", "Singapore Airlines"),
    ("SLK", "MI", "SilkAir"),
    ("TGW", "TR", "Scoot"),
    ("MAS", "MH", "Malaysia Airlines"),
    ("AXM", "D7", "AirAsia X"),
    ("AIQ", "QZ", "AirAsia"),
    ("IAW", "BI", "Royal Brunei"),
    ("THA", "TG", "Thai Airways"),
    ("TVJ", "VZ", "Thai Vietjet"),
    ("GAR", "GA", "Garuda Indonesia"),
    ("LNI", "JT", "Lion Air"),
    ("BTK", "ID", "Batik Air"),
    ("VJC", "VJ", "Vietjet Air"),
    ("HVN", "VN", "Vietnam Airlines"),
    ("PAL", "PR", "Philippine Airlines"),
    ("CEB", "5J", "Cebu Pacific"),
    ("QFA", "QF", "Qantas"),
    ("JST", "JQ", "Jetstar"),
    ("VOZ", "VA", "Virgin Australia"),
    ("ANZ", "NZ", "Air New Zealand"),
    ("AIX", "IX", "Air India Express"),
    ("AIC", "AI", "Air India"),
    ("IGO", "6E", "IndiGo"),
    ("SEJ", "SG", "SpiceJet"),
    ("CPA", "CX", "Cathay Pacific"),

    # ── Middle East & Africa ─────────────────────────────────────────────────
    ("SVA", "SV", "Saudia"),
    ("FDB", "FZ", "flydubai"),
    ("ABY", "G9", "Air Arabia"),
    ("OAL", "OA", "Olympic Air"),
    ("MSR", "MS", "EgyptAir"),
    ("ETH", "ET", "Ethiopian Airlines"),
    ("KQA", "KQ", "Kenya Airways"),
    ("SAA", "SA", "South African Airways"),

    # ── Latin America ─────────────────────────────────────────────────────────
    ("LAN", "LA", "LATAM Airlines"),
    ("TAM", "JJ", "LATAM Brasil"),
    ("GLO", "G3", "Gol Linhas Aéreas"),
    ("AZU", "AD", "Azul Brazilian"),
    ("AVA", "AV", "Avianca"),
    ("VOI", "Y4", "Volaris"),
    ("AMX", "AM", "Aeromexico"),
    ("VIV", "VB", "VivaAerobus"),

    # ── Russia / CIS ─────────────────────────────────────────────────────────
    ("AFL", "SU", "Aeroflot"),
    ("SDM", "FV", "Rossiya Airlines"),
    ("SVP", "UT", "UTair"),
]


# ─── Conversion helpers ───────────────────────────────────────────────────────

def rgb_to_rgb565(r: int, g: int, b: int) -> int:
    """Convert 8-bit RGB to 16-bit RGB565."""
    r5 = (r >> 3) & 0x1F
    g6 = (g >> 2) & 0x3F
    b5 = (b >> 3) & 0x1F
    return (r5 << 11) | (g6 << 5) | b5


def image_to_rgb565_blob(img: Image.Image, width: int, height: int) -> bytes:
    """
    Resize img to (width, height) using high-quality downsampling, then
    convert each pixel to RGB565. Fully-transparent pixels (alpha < 16) are
    encoded as TRANSPARENT_RGB565 (0xF81F).
    """
    img = img.convert("RGBA")
    img = img.resize((width, height), Image.LANCZOS)

    blob = bytearray()
    pixels = img.load()

    for y in range(height):
        for x in range(width):
            r, g, b, a = pixels[x, y]
            if a < 16:
                pixel16 = TRANSPARENT_RGB565
            else:
                # Blend onto black background proportionally to alpha.
                r = int(r * a / 255)
                g = int(g * a / 255)
                b = int(b * a / 255)
                pixel16 = rgb_to_rgb565(r, g, b)
                if pixel16 == TRANSPARENT_RGB565:
                    pixel16 = 0xF820  # slightly off-magenta
            blob += struct.pack('<H', pixel16)

    return bytes(blob)


# ─── Main ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Build local RGB565 airline logo blobs from an image folder for FlightWall ESP32."
    )
    parser.add_argument("--in-dir", type=Path, default=DEFAULT_IN,
                        help=f"Directory containing source images (default: {DEFAULT_IN})")
    parser.add_argument("--width",  type=int, default=DEFAULT_WIDTH,
                        help=f"Logo width in pixels (default: {DEFAULT_WIDTH})")
    parser.add_argument("--height", type=int, default=DEFAULT_HEIGHT,
                        help=f"Logo height in pixels (default: {DEFAULT_HEIGHT})")
    parser.add_argument("--out",    type=Path, default=DEFAULT_OUT,
                        help=f"Output directory (default: {DEFAULT_OUT})")
    parser.add_argument("--skip-existing", action="store_true",
                        help="Skip processing if the target .bin file already exists")
    args = parser.parse_args()

    in_dir: Path = args.in_dir
    out_dir: Path = args.out

    if not in_dir.exists():
        print(f"ERROR: Input directory '{in_dir}' does not exist.")
        print("Please create it and place your downloaded logo images inside.")
        sys.exit(1)

    out_dir.mkdir(parents=True, exist_ok=True)

    width:  int = args.width
    height: int = args.height
    expected_bytes = width * height * 2

    # Map lookups for the file scanner
    airline_map = {icao.upper(): (iata, name) for icao, iata, name in AIRLINE_LIST}

    # Find valid image files in the directory
    valid_extensions = {'.png', '.jpg', '.jpeg', '.bmp', '.webp'}
    image_files = [p for p in in_dir.iterdir() if p.suffix.lower() in valid_extensions]

    print(f"FlightWall Local Logo Builder")
    print(f"  Input dir   : {in_dir}")
    print(f"  Output dir  : {out_dir}")
    print(f"  Logo size   : {width}x{height} px ({expected_bytes} bytes each)")
    print(f"  Found files : {len(image_files)} source images")
    print()

    success = 0
    skipped = 0
    failed  = 0
    unmapped = 0

    for idx, img_path in enumerate(image_files, start=1):
        # Infer ICAO from filename (e.g. "aal.png" or "AAL_logo.png" -> "AAL")
        file_stem = img_path.stem.split('_')[0].upper()
        
        if file_stem not in airline_map:
            print(f"[{idx:3d}/{len(image_files)}] {img_path.name} — SKIPPED (No matching ICAO in script list)")
            unmapped += 1
            continue

        iata, name = airline_map[file_stem]
        out_path = out_dir / f"{file_stem}.bin"
        prefix = f"[{idx:3d}/{len(image_files)}] {file_stem} ({iata}) {name}"

        if args.skip_existing and out_path.exists():
            print(f"{prefix} — skipped (exists)")
            skipped += 1
            continue

        print(f"{prefix} — processing...", end=" ", flush=True)

        try:
            with Image.open(img_path) as img:
                blob = image_to_rgb565_blob(img, width, height)
                assert len(blob) == expected_bytes, f"blob size {len(blob)} != {expected_bytes}"
                out_path.write_bytes(blob)
                print(f"OK ({len(blob)} bytes)")
                success += 1
        except Exception as e:
            print(f"FAILED ({e})")
            failed += 1

    print()
    print("─" * 60)
    print(f"Done.  {success} OK  |  {skipped} skipped  |  {failed} failed  |  {unmapped} unmapped")

    if out_dir.exists():
        total_kb = sum(p.stat().st_size for p in out_dir.glob("*.bin")) / 1024
        print(f"Total logo data: {total_kb:.1f} KB in {out_dir}")
    
    print()
    print("Next steps:")
    print("  cd firmware")
    print("  pio run --target uploadfs")


if __name__ == "__main__":
    main()