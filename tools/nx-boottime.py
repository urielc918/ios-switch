# Userland start time from a decoded SD log: first launchd "[System Event] [T]"
# -> the last System Event before "exec SpringBoard"; mach ticks at 19.2 MHz.
import re, sys
for path in sys.argv[1:]:
    first = last = None
    for line in open(path, errors='replace'):
        m = re.match(r'\[System Event\] \[(\d+)\]', line)
        if m:
            t = int(m.group(1)); first = first or t; last = t
        if 'exec SpringBoard' in line:
            break
    print(f"{path}: {(last - first) / 19.2e6:.2f} s" if first and last else f"{path}: no events")
