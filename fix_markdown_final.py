#!/usr/bin/env python3
"""
修复Markdown文件：删除连续的空行，保留单个空行作为段落分隔
策略：
1. 删除连续的空行（多个空行变成一个）
2. 在标题后保留一个空行（如果下一行不是标题）
3. 在分隔线前后保留一个空行
4. 代码块内的内容完全保持原样
5. 列表项之间不添加空行（保持紧凑）
"""
import re
import sys
import os

def is_title_line(line):
    """判断是否是标题行"""
    if not line.strip():
        return False
    return line.strip().startswith('#')

def is_separator_line(line):
    """判断是否是分隔线"""
    stripped = line.strip()
    return bool(re.match(r'^---+$', stripped))

def is_list_item(line):
    """判断是否是列表项"""
    stripped = line.strip()
    if not stripped:
        return False
    # 无序列表、有序列表、任务列表
    return bool(re.match(r'^[\s]*[-*+]\s+', stripped) or 
                re.match(r'^[\s]*\d+[\.\)]\s+', stripped) or
                re.match(r'^[\s]*- \[', stripped))

def process_markdown_file(input_file, output_file):
    """处理Markdown文件"""
    with open(input_file, 'r', encoding='utf-8') as f:
        lines = f.readlines()
    
    output_lines = []
    in_code_block = False
    prev_line_was_blank = False
    prev_line = None
    blank_line_count = 0
    
    for i, line in enumerate(lines):
        stripped = line.strip()
        original_line = line.rstrip()
        
        # 处理代码块
        if stripped.startswith('```'):
            # 代码块结束前，如果有累积的空行，先处理
            if in_code_block:
                # 代码块结束
                in_code_block = False
            else:
                # 代码块开始
                in_code_block = True
                # 如果之前有空行，在代码块前添加一个空行
                if prev_line_was_blank and output_lines and output_lines[-1].strip():
                    output_lines.append('')
            
            output_lines.append(original_line)
            prev_line_was_blank = False
            blank_line_count = 0
            prev_line = original_line
            continue
        
        # 代码块内的内容完全保持原样
        if in_code_block:
            output_lines.append(original_line)
            prev_line_was_blank = (not stripped)
            prev_line = original_line
            continue
        
        # 代码块外的处理
        if not stripped:
            # 空行：累积计数
            blank_line_count += 1
            prev_line_was_blank = True
            prev_line = None
            continue
        
        # 非空行处理
        # 处理之前累积的空行
        if blank_line_count > 0:
            # 判断是否需要保留一个空行
            should_keep_blank = False
            
            # 规则1：当前行是标题，且上一行不是标题且不是分隔线
            if is_title_line(original_line):
                if prev_line and prev_line.strip():
                    if not is_title_line(prev_line) and not is_separator_line(prev_line):
                        should_keep_blank = True
            
            # 规则2：当前行是分隔线，且上一行不是空行
            elif is_separator_line(original_line):
                if prev_line and prev_line.strip():
                    should_keep_blank = True
            
            # 规则3：上一行是标题或分隔线，当前行不是标题
            elif prev_line:
                if (is_title_line(prev_line) or is_separator_line(prev_line)) and not is_title_line(original_line):
                    should_keep_blank = True
            
            # 规则4：上一行是列表项，当前行是另一个列表项的开始（不同列表）
            # 这个比较难判断，暂时不处理
            
            if should_keep_blank and blank_line_count > 0:
                output_lines.append('')
            
            blank_line_count = 0
            prev_line_was_blank = False
        
        # 添加当前行
        output_lines.append(original_line)
        prev_line = original_line
    
    # 处理文件末尾的空行
    # 如果末尾有多个空行，只保留一个
    while output_lines and not output_lines[-1].strip():
        output_lines.pop()
    # 文件末尾添加一个换行符
    if output_lines and output_lines[-1].strip():
        output_lines.append('')
    
    # 写入输出文件
    with open(output_file, 'w', encoding='utf-8') as f:
        f.write('\n'.join(output_lines))
        if not output_lines or output_lines[-1]:
            f.write('\n')
    
    return len([l for l in output_lines if l.strip()])

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
        else:
            print(f"Warning: {backup_file} not found, processing {filename} directly...")
        
        # 处理文件
        try:
            line_count = process_markdown_file(filename, filename)
            print(f"Processed {filename}: removed excessive blank lines, kept single blank lines for readability")
        except Exception as e:
            print(f"Error processing {filename}: {e}")
            import traceback
            traceback.print_exc()

if __name__ == '__main__':
    main()

