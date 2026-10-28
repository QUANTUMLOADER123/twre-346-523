#!/usr/bin/env python3
"""Extract the latest release notes section from release_notes.md"""
import re
import sys
import os

def main():
    version = os.environ.get('VERSION', '')
    if not version:
        print("VERSION env var not set", file=sys.stderr)
        sys.exit(1)
    
    with open('.github/release_notes.md', 'r', encoding='utf-8') as f:
        content = f.read()
    
    # Try to find the section for this version
    pattern = r'### Что нового в ' + re.escape(version) + r'.*?(?=### Что нового в|$)'
    match = re.search(pattern, content, re.DOTALL)
    
    if match:
        section = match.group(0).strip()
    else:
        # Take the first section after the table
        lines = content.split('\n')
        section_lines = []
        in_section = False
        for line in lines:
            if line.startswith('### Что нового в'):
                if not in_section:
                    in_section = True
                    section_lines.append(line)
                else:
                    break
            elif in_section:
                section_lines.append(line)
        section = '\n'.join(section_lines).strip()
    
    # Add file table at the top
    table = """## Counter-Strike: HvH

| Файл | Что это |
|---|---|
| **CounterStrikeHvH-windows.zip** | Полная Windows-версия: распаковать, запустить `cshvh.exe` |
| **cshvh.exe** | Только exe (класть рядом с папкой `assets` из архива / репозитория) |
| **cshvh.apk** | Android (arm64 / armv7, Android 7.0+, OpenGL ES 3) |

"""
    
    with open('/tmp/release_notes.txt', 'w', encoding='utf-8') as f:
        f.write(table + section + '\n')
    
    print(f"Extracted notes for {version}")

if __name__ == '__main__':
    main()
