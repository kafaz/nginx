#!/usr/bin/env python3
"""
修复Markdown文件：删除连续的空行，在合适位置保留单个空行以提高可读性
策略：
1. 删除连续的空行（多个空行变成一个）
2. 在以下位置保留单个空行：
   - 标题后（如果下一行不是标题且不是列表）
   - 分隔线前后
   - 段落之间（如果上一行是普通文本，当前行也是普通文本或列表）
   - 代码块前后
3. 代码块内的内容完全保持原样
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

def is_bold_line(line):
    """判断是否是加粗文本行（如 **目标**：）"""
    if not line:
        return False
    stripped = line.strip()
    return bool(re.match(r'^\*\*.*\*\*', stripped))

def is_code_block_marker(line):
    """判断是否是代码块标记"""
    if not line:
        return False
    return line.strip().startswith('```')

def process_markdown_file(input_file, output_file):
    """处理Markdown文件"""
    with open(input_file, 'r', encoding='utf-8') as f:
        lines = f.readlines()
    
    output_lines = []
    in_code_block = False
    pending_blanks = 0
    last_non_blank_line = None
    last_non_blank_index = -1
    
    i = 0
    while i < len(lines):
        line = lines[i]
        stripped = line.strip()
        original_line = line.rstrip()
        
        # 处理代码块标记
        if is_code_block_marker(stripped):
            # 处理代码块前的空行
            if pending_blanks > 0:
                if last_non_blank_line and not is_code_block_marker(last_non_blank_line):
                    output_lines.append('')
                pending_blanks = 0
            
            if not in_code_block:
                in_code_block = True
            else:
                in_code_block = False
            
            output_lines.append(original_line)
            last_non_blank_line = original_line
            last_non_blank_index = len(output_lines) - 1
            i += 1
            continue
        
        # 代码块内的内容完全保持原样
        if in_code_block:
            output_lines.append(original_line)
            last_non_blank_line = original_line
            last_non_blank_index = len(output_lines) - 1
            i += 1
            continue
        
        # 代码块外的处理
        if not stripped:
            pending_blanks += 1
            i += 1
            continue
        
        # 非空行：处理之前累积的空行
        should_add_blank = False
        
        if pending_blanks > 0:
            current_is_title = is_title_line(original_line)
            current_is_separator = is_separator_line(original_line)
            current_is_list = is_list_item(original_line)
            current_is_bold = is_bold_line(original_line)
            
            if last_non_blank_line:
                last_was_title = is_title_line(last_non_blank_line)
                last_was_separator = is_separator_line(last_non_blank_line)
                last_was_list = is_list_item(last_non_blank_line)
                last_was_bold = is_bold_line(last_non_blank_line)
            else:
                last_was_title = False
                last_was_separator = False
                last_was_list = False
                last_was_bold = False
            
            # 规则1：当前行是标题
            if current_is_title:
                # 如果上一行不是标题且不是分隔线，添加空行
                if last_non_blank_line:
                    if not last_was_title and not last_was_separator:
                        should_add_blank = True
                    # 如果上一行是列表，标题前应该有空行
                    elif last_was_list:
                        should_add_blank = True
            
            # 规则2：当前行是分隔线
            elif current_is_separator:
                if last_non_blank_line and not last_was_separator:
                    should_add_blank = True
            
            # 规则3：上一行是分隔线
            elif last_was_separator:
                if not current_is_title:
                    should_add_blank = True
            
            # 规则4：上一行是标题
            elif last_was_title:
                # 标题后通常应该有空行（除非紧跟着列表）
                # 但为了可读性，我们总是添加空行
                should_add_blank = True
            
            # 规则5：上一行是加粗文本（如 **目标**：），当前行不是列表
            elif last_was_bold:
                if not current_is_list and not current_is_title:
                    should_add_blank = True
            
            # 规则6：上一行是列表，当前行不是列表且不是标题
            elif last_was_list:
                if not current_is_list and not current_is_title:
                    should_add_blank = True
            
            # 规则7：普通段落之间（上一行和当前行都不是标题、列表、加粗等）
            elif last_non_blank_line:
                if (not last_was_title and not last_was_list and not last_was_bold and
                    not current_is_title and not current_is_list and not current_is_bold):
                    # 检查是否是段落分隔（通过内容判断）
                    # 如果上一行以句号、问号、感叹号结尾，可能是段落结束
                    last_line = last_non_blank_line.strip()
                    if last_line and last_line[-1] in '。！？.!?':
                        should_add_blank = True
            
            if should_add_blank:
                output_lines.append('')
            
            pending_blanks = 0
        
        # 添加当前行
        output_lines.append(original_line)
        last_non_blank_line = original_line
        last_non_blank_index = len(output_lines) - 1
        i += 1
    
    # 删除文件末尾的空行
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
            print(f"Processed {filename}: {line_count} lines (removed excessive blank lines, kept single blank lines for readability)")
        except Exception as e:
            print(f"Error processing {filename}: {e}")
            import traceback
            traceback.print_exc()

if __name__ == '__main__':
    main()

