"""BOM + placement (CPL) files in JLCPCB's format for both boards.
Run with KiCad's python:  "C:\Program Files\KiCad\9.0\bin\python.exe" make_assembly_files.py
Positions come from the footprint origins in the .kicad_pcb files (bottom-left aux origin not used:
JLCPCB aligns placements to the Gerbers itself and shows a preview to confirm)."""
import csv, collections, pcbnew
MPN = {  # value/ref pattern -> (comment, manufacturer part number)
 'RJ45': ('RJ45 8P8C THT', '54602-908LF'),
}
def part_for(ref, val, fp):
    v = val
    if ref.startswith('J') and '54602' in (val + fp): return ('RJ45 8P8C horizontal THT', '54602-908LF')
    if ref in ('U2', 'U3', 'U4'): return ('CD74HCT4067 SOIC-24W', 'CD74HCT4067M96')
    if ref == 'U5': return ('SN74AHCT125N DIP-14', 'SN74AHCT125N')
    if ref.startswith('RN'): return ('10k x8 bussed SIP-9', '4609X-101-103LF')
    if ref == 'J_PWR': return ('DC barrel jack 5.5x2.1', 'PJ-102AH')
    return (v, '')
for name in ('hub', 'node'):
    b = pcbnew.LoadBoard(f'{name}/hoard_{name}.kicad_pcb')
    rows, groups = [], collections.OrderedDict()
    for f in sorted(b.GetFootprints(), key=lambda f: f.GetReference()):
        ref, val = f.GetReference(), f.GetValue()
        fp = str(f.GetFPID().GetLibItemName())
        if ref.startswith(('H', 'MH')) or 'MountingHole' in fp: continue
        p = f.GetPosition()
        rows.append([ref, f'{pcbnew.ToMM(p.x):.3f}mm', f'{-pcbnew.ToMM(p.y):.3f}mm',
                     'Bottom' if f.IsFlipped() else 'Top', f'{f.GetOrientationDegrees():.0f}'])
        c, m = part_for(ref, val, fp)
        groups.setdefault((c, fp, m), []).append(ref)
    with open(f'{name}/hoard_{name}_cpl.csv', 'w', newline='') as fh:
        w = csv.writer(fh); w.writerow(['Designator', 'Mid X', 'Mid Y', 'Layer', 'Rotation']); w.writerows(rows)
    with open(f'{name}/hoard_{name}_bom.csv', 'w', newline='') as fh:
        w = csv.writer(fh); w.writerow(['Comment', 'Designator', 'Footprint', 'Manufacturer Part Number'])
        for (c, fp, m), refs in groups.items(): w.writerow([c, ','.join(refs), fp, m])
    print(name, len(rows), 'parts')
    for (c, fp, m), refs in groups.items(): print('  ', len(refs), c, '|', fp, '|', m, '|', ','.join(refs)[:60])
