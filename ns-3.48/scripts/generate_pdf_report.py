#!/usr/bin/env python3
"""
generate_pdf_report.py

Converts report/technical_report.md into a formatted PDF using LibreOffice headless conversion.
"""

import os
import re
import subprocess

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    report_md = os.path.join(project_root, 'report', 'technical_report.md')
    report_html = os.path.join(project_root, 'report', 'technical_report.html')
    report_pdf = os.path.join(project_root, 'report', 'technical_report.pdf')
    report_dir = os.path.join(project_root, 'report')

    with open(report_md, 'r', encoding='utf-8') as f:
        text = f.read()

    html_content = """<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>PS-016 Technical Report - LostPacket</title>
<style>
body {
    font-family: 'Liberation Sans', 'DejaVu Sans', Arial, sans-serif;
    line-height: 1.5;
    margin: 40px;
    color: #222;
    font-size: 11pt;
}
h1 {
    color: #1a365d;
    font-size: 18pt;
    border-bottom: 2px solid #2b6cb0;
    padding-bottom: 6px;
    margin-top: 10px;
}
h2 {
    color: #2b6cb0;
    font-size: 14pt;
    margin-top: 22px;
    border-bottom: 1px solid #cbd5e0;
    padding-bottom: 4px;
}
h3 {
    color: #2d3748;
    font-size: 12pt;
    margin-top: 15px;
}
p {
    margin: 8px 0;
    text-align: justify;
}
table {
    border-collapse: collapse;
    width: 100%;
    margin: 14px 0;
    font-size: 7.5pt;
}
th, td {
    border: 1px solid #a0aec0;
    padding: 4px 4px;
    text-align: center;
}
th {
    background-color: #edf2f7;
    color: #1a202c;
    font-weight: bold;
}
tr:nth-child(even) {
    background-color: #f7fafc;
}
code {
    background-color: #edf2f7;
    padding: 2px 4px;
    border-radius: 3px;
    font-family: monospace;
    font-size: 10pt;
}
pre {
    background-color: #f7fafc;
    border: 1px solid #e2e8f0;
    padding: 12px;
    border-radius: 4px;
    overflow-x: auto;
    font-family: monospace;
    font-size: 9.5pt;
}
hr {
    border: 0;
    border-top: 1px solid #cbd5e0;
    margin: 20px 0;
}
</style>
</head>
<body>
"""

    lines = text.split('\n')
    in_table = False
    table_html = []
    in_code_block = False
    code_block = []

    for line in lines:
        if line.strip().startswith('```'):
            if in_code_block:
                in_code_block = False
                html_content += '<pre>' + '\n'.join(code_block) + '</pre>\n'
                code_block = []
            else:
                in_code_block = True
            continue

        if in_code_block:
            code_block.append(line.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;'))
            continue

        if line.startswith('# '):
            html_content += f'<h1>{line[2:].strip()}</h1>\n'
        elif line.startswith('## '):
            html_content += f'<h2>{line[3:].strip()}</h2>\n'
        elif line.startswith('### '):
            html_content += f'<h3>{line[4:].strip()}</h3>\n'
        elif line.strip().startswith('![') and '](' in line:
            if in_table:
                in_table = False
                table_html.append('</table>')
                html_content += '\n'.join(table_html) + '\n'
                table_html = []
            m = re.match(r'!\[(.*?)\]\((.*?)\)', line.strip())
            if m:
                alt = m.group(1)
                src = m.group(2)
                # Resolve relative path
                if not os.path.isabs(src):
                    src = os.path.normpath(os.path.join(os.path.dirname(report_md), src))
                html_content += f'<div style="text-align: center; margin: 16px 0;"><img src="{src}" style="max-width: 90%; height: auto; border: 1px solid #cbd5e0; border-radius: 4px;"><div style="font-size: 9pt; color: #4a5568; font-style: italic; margin-top: 4px;">{alt}</div></div>\n'
            continue
        elif line.strip() == '---':
            html_content += '<hr>\n'
        elif line.startswith('|') and '|' in line[1:]:
            if not in_table:
                in_table = True
                table_html = ['<table>']
            cells = [c.strip() for c in line.split('|')[1:-1]]
            if all(set(c) <= set('-: ') for c in cells):
                continue
            tag = 'th' if len(table_html) == 1 else 'td'
            row_html = '<tr>' + ''.join(f'<{tag}>{c}</{tag}>' for c in cells) + '</tr>'
            table_html.append(row_html)
        else:
            if in_table:
                in_table = False
                table_html.append('</table>')
                html_content += '\n'.join(table_html) + '\n'
                table_html = []

            if line.strip():
                formatted = line.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')
                formatted = re.sub(r'\*\*(.*?)\*\*', r'<b>\g<1></b>', formatted)
                formatted = re.sub(r'\*(.*?)\*', r'<i>\g<1></i>', formatted)
                formatted = re.sub(r'`(.*?)`', r'<code>\g<1></code>', formatted)
                html_content += f'<p>{formatted}</p>\n'

    if in_table:
        table_html.append('</table>')
        html_content += '\n'.join(table_html) + '\n'

    html_content += '</body></html>'

    with open(report_html, 'w', encoding='utf-8') as f:
        f.write(html_content)

    print("HTML report written to:", report_html)
    cmd = ['libreoffice', '--headless', '--convert-to', 'pdf', report_html, '--outdir', report_dir]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode == 0:
        print("PDF report generated successfully:", report_pdf)
    else:
        print("LibreOffice PDF conversion error:", res.stderr)

if __name__ == '__main__':
    main()
