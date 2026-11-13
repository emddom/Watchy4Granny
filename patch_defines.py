import re

with open('src/Defines_GSR.h', 'r') as f:
    lines = f.readlines()

new_lines = []
for line in lines:
    if '#define GSR_MENU_OPTIONS 4' in line:
        new_lines.append(line)
        new_lines.append('#define GSR_MENU_UTILS 5\n')
        continue
    if '#define GSR_MENU_INTROUBLE 4' in line:
        new_lines.append(line)
        new_lines.append('#define GSR_MENU_INUTILS 5\n')
        continue
        
    m = re.match(r'^#define\s+(GSR_MENU_[A-Z0-9_]+)\s+(\d+)(.*)$', line)
    if m:
        name = m.group(1)
        val = int(m.group(2))
        rest = m.group(3)
        if val >= 5 and name != 'GSR_MENU_INUTILS':
            # wait, if it's the IN_ prefix, don't shift by default unless it's over 4
            if name.startswith('GSR_MENU_IN'):
                new_lines.append(line) # don't shift INNORMAL etc, we manually added INUTILS
            else:
                new_lines.append(f'#define {name} {val+1}{rest}\n')
        else:
            new_lines.append(line)
    else:
        new_lines.append(line)

new_lines.append('#define GSR_MENU_UTILS_BTN 40\n')
new_lines.append('#define GSR_MENU_UTILS_FIND 41\n')
new_lines.append('#define GSR_MENU_UTILS_MED 42\n')
new_lines.append('#define GSR_MENU_UTILS_WEAT 43\n')

with open('src/Defines_GSR.h', 'w') as f:
    f.writelines(new_lines)
