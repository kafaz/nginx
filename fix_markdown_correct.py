#!/usr/bin/env python3
"""
修复Markdown文件：删除连续的空行，在合适位置保留单个空行
策略：
1. 遍历所有行，累积连续的空行
2. 当遇到非空行时，根据上下文决定是否保留一个空行
3. 保留空行的规则：
   - 标题前（如果上一行不是标题）
   - 分隔线前后
   - 代码块前后
4. 代码块内的内容完全保持原样
"""
import re
import sys
import os

def is_title_line(line):
    """判断是否是标题行"""
    if not line or not line.strip():
        return False
    return line.strip().startswith('#')

def is_separator_line(line):
    """判断是否是分隔线"""
    if not line:
        return False
    stripped = line.strip()
    return bool(re.match(r'^---+$', stripped))

def is_list_item(line):
    """判断是否是列表项"""
    if not line:
        return False
    stripped = line.strip()
    if not stripped:
        return False
    return bool(re.match(r'^[\s]*[-*+]\s+', stripped) or 
                re.match(r'^[\s]*\d+[\.\)]\s+', stripped) or
                re.match(r'^[\s]*- \[', stripped))

def process_markdown_file(input_file, output_file):
    """处理Markdown文件"""
    with open(input_file, 'r', encoding='utf-8') as f:
        lines = f.readlines()
    
    output_lines = []
    in_code_block = False
    pending_blanks = 0  # 待处理的空行数量
    last_non_blank_line = None  # 上一个非空行的内容
    
    i = 0
    while i < len(lines):
        line = lines[i]
        stripped = line.strip()
        original_line = line.rstrip()
        
        # 处理代码块标记
        if stripped.startswith('```'):
            # 处理代码块前的空行
            if pending_blanks > 0:
                # 如果上一个非空行存在且不是代码块标记，在代码块前添加一个空行
                if last_non_blank_line and not last_non_blank_line.strip().startswith('```'):
                    output_lines.append('')
                pending_blanks = 0
            
            if not in_code_block:
                in_code_block = True
            else:
                in_code_block = False
            
            output_lines.append(original_line)
            last_non_blank_line = original_line
            i += 1
            continue
        
        # 代码块内的内容完全保持原样（包括空行）
        if in_code_block:
            output_lines.append(original_line)
            last_non_blank_line = original_line
            i += 1
            continue
        
        # 代码块外的处理
        if not stripped:
            # 空行：累积计数
            pending_blanks += 1
            i += 1
            continue
        
        # 非空行：处理之前累积的空行
        should_add_blank = False
        
        if pending_blanks > 0:
            # 判断是否需要保留一个空行
            current_is_title = is_title_line(original_line)
            current_is_separator = is_separator_line(original_line)
            last_was_title = last_non_blank_line and is_title_line(last_non_blank_line)
            last_was_separator = last_non_blank_line and is_separator_line(last_non_blank_line)
            
            # 规则1：当前行是标题，且上一行不是标题且不是分隔线
            if current_is_title:
                if last_non_blank_line:
                    if not last_was_title and not last_was_separator:
                        should_add_blank = True
                elif output_lines:  # 文件开头的标题前不加空行
                    pass
            
            # 规则2：当前行是分隔线，且上一行存在
            elif current_is_separator:
                if last_non_blank_line:
                    should_add_blank = True
            
            # 规则3：上一行是分隔线，当前行不是标题（分隔线后应该有空行）
            elif last_was_separator:
                if not current_is_title:
                    should_add_blank = True
            
            # 规则4：上一行是标题，当前行不是标题且不是列表项（标题后应该有空行）
            elif last_was_title:
                if not current_is_title and not is_list_item(original_line):
                    # 但如果是紧跟着的列表项，通常不需要空行
                    # 这里我们保留空行以增加可读性
                    should_add_blank = True
            
            if should_add_blank:
                output_lines.append('')
            
            pending_blanks = 0
        
        # 添加当前行
        output_lines.append(original_line)
        last_non_blank_line = original_line
        i += 1
    
    # 处理文件末尾：删除末尾的空行
    while output_lines and not output_lines[-1].strip():
        output_lines.pop()
    
    # 写入输出文件
    with open(output_file, 'w', encoding='utf-8') as f:
        f.write('\n'.join(output_lines))
        if output_lines:
            f.write('\n')
    
    return len(output_lines)

def main():
    """主函数"""
    files_to_process = [
        'NGINX_STUDY_PLAN.md',
        'nginx.md',
    ]
    
    for filename in files_to_process:
        if not os.path.exists(filename):
            print(f"Warning: {filename} not found, skipping...")
            continue
        
        # 从备份恢复
        backup_file = filename + '.bak'
        if os.path.exists(backup_file):
            import shutil
            shutil.copy2(backup_file, filename)
            print(f"Restored from backup: {backup_file}")
        
        # 处理文件
        try:
            line_count = process_markdown_file(filename, filename)
            print(f"Processed {filename}: {line_count} lines (removed excessive blank lines, kept single blank lines for paragraph separation)")
        except Exception as e:
            print(f"Error processing {filename}: {e}")
            import traceback
            traceback.print_exc()

if __name__ == '__main__':
    main()

