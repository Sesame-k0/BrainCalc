with open('apps/code/menu_controller.cpp', 'r', encoding='utf-8', errors='ignore') as f:
    lines = f.readlines()
new_lines = []
for i, line in enumerate(lines):
    if 'ScriptStore::loadScriptsFromDirectory();' in line:
        if i < 140 or i > 180:
            continue
    new_lines.append(line)
with open('apps/code/menu_controller.cpp', 'w', encoding='utf-8', newline='') as f:
    f.writelines(new_lines)
print('Line filtered successfully')
