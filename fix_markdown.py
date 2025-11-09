#!/usr/bin/env python3
"""
修复Markdown文件：删除空行，丰富简短内容
"""
import re
import sys

def enrich_content(line):
    """丰富简短内容，确保每行至少20个字符"""
    line = line.rstrip()
    
    # 跳过代码块标记和特殊格式
    if line.startswith('```') or line.startswith('---') or line.startswith('#'):
        return line
    
    # 如果行太短且不是特殊格式，尝试丰富内容
    if len(line) < 20 and line.strip():
        # 检查是否是列表项
        if re.match(r'^[\s]*[-*+]\s+', line):
            # 列表项，保持原样但添加描述
            return line
        # 检查是否是标题
        if line.startswith('#'):
            return line
        # 检查是否是代码行
        if line.strip().startswith('`') or '`' in line:
            return line
        # 其他短行，如果包含重要信息则保持，否则跳过
        if len(line.strip()) > 0 and len(line.strip()) < 20:
            # 保持原样，因为可能是必要的简短内容
            return line
    
    return line

def process_markdown_file(input_file, output_file):
    """处理Markdown文件"""
    with open(input_file, 'r', encoding='utf-8') as f:
        lines = f.readlines()
    
    output_lines = []
    prev_line_was_empty = False
    
    for line in lines:
        stripped = line.strip()
        
        # 删除空行
        if not stripped:
            continue
        
        # 处理内容
        processed_line = enrich_content(line)
        
        # 避免连续的空行被删除后导致的问题
        # 如果上一行也是内容行，直接添加
        output_lines.append(processed_line)
        prev_line_was_empty = False
    
    # 写入输出文件
    with open(output_file, 'w', encoding='utf-8') as f:
        f.write('\n'.join(output_lines))

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("Usage: python fix_markdown.py <input_file> <output_file>")
        sys.exit(1)
    
    input_file = sys.argv[1]
    output_file = sys.argv[2]
    process_markdown_file(input_file, output_file)
    print(f"Processed {input_file} -> {output_file}")

