#!/usr/bin/env python3
"""
智能修复Markdown文件：
1. 删除连续的多余空行（多个空行合并为一个）
2. 保留单个空行作为段落分隔
3. 删除代码块外的多余空行
4. 保持代码块内的格式（包括必要的空行）
"""
import re
import sys
import os

def is_title_line(line):
    """判断是否是标题行"""
    stripped = line.strip()
    return stripped.startswith('#') and len(stripped) > 0

def is_list_item(line):
    """判断是否是列表项"""
    stripped = line.strip()
    # 无序列表
    if re.match(r'^[\s]*[-*+]\s+', stripped):
        return True
    # 有序列表
    if re.match(r'^[\s]*\d+[\.\)]\s+', stripped):
        return True
    # 任务列表
    if re.match(r'^[\s]*- \[', stripped):
        return True
    return False

def is_separator_line(line):
    """判断是否是分隔线"""
    stripped = line.strip()
    # 分隔线（至少3个-）
    if re.match(r'^---+$', stripped):
        return True
    return False

def should_preserve_single_blank_before(line, prev_line_was_blank, prev_line):
    """判断是否应该在当前行前保留一个空行"""
    stripped = line.strip()
    
    # 如果是空行，不处理
    if not stripped:
        return False
    
    # 标题前应该有空行（如果上一行不是标题且不是空行）
    if is_title_line(line):
        if prev_line and prev_line.strip() and not is_title_line(prev_line) and not is_separator_line(prev_line):
            return True
    
    # 分隔线前后应该有空行
    if is_separator_line(line):
        if prev_line and prev_line.strip() and not is_separator_line(prev_line):
            return True
    
    # 列表项前的处理：如果上一行不是列表项且不是空行，可能需要空行
    # 但为了紧凑，我们不在列表项前添加空行
    
    return False

def process_markdown_file(input_file, output_file):
    """处理Markdown文件"""
    with open(input_file, 'r', encoding='utf-8') as f:
        lines = f.readlines()
    
    output_lines = []
    in_code_block = False
    prev_line_was_blank = False
    prev_line = None
    
    for i, line in enumerate(lines):
        stripped = line.strip()
        original_line = line.rstrip()
        
        # 处理代码块
        if stripped.startswith('```'):
            # 代码块结束前，如果之前有空行，先输出一个空行作为分隔
            if in_code_block and prev_line_was_blank:
                # 代码块结束，不需要保留之前的空行
                pass
            elif not in_code_block and prev_line_was_blank:
                # 代码块开始前，保留一个空行
                output_lines.append('')
            
            if not in_code_block:
                in_code_block = True
            else:
                in_code_block = False
            
            output_lines.append(original_line)
            prev_line_was_blank = False
            prev_line = original_line
            continue
        
        # 代码块内的内容完全保持原样（包括空行）
        if in_code_block:
            output_lines.append(original_line)
            prev_line_was_blank = (not stripped)
            prev_line = original_line
            continue
        
        # 代码块外的处理
        if not stripped:
            # 空行：如果上一个也是空行，跳过（删除连续空行）
            if not prev_line_was_blank:
                # 这是第一个空行，暂时标记，不立即添加
                prev_line_was_blank = True
            # 如果上一个已经是空行，跳过这个（删除连续空行）
            prev_line = None
            continue
        
        # 非空行处理
        # 如果之前有空行标记，且当前行需要前置空行，则添加
        if prev_line_was_blank:
            if should_preserve_single_blank_before(original_line, prev_line_was_blank, prev_line):
                output_lines.append('')
            # 重置空行标记
            prev_line_was_blank = False
        
        # 添加当前行
        output_lines.append(original_line)
        prev_line = original_line
    
    # 写入输出文件
    with open(output_file, 'w', encoding='utf-8') as f:
        f.write('\n'.join(output_lines))
        # 确保文件以换行符结尾
        if output_lines and output_lines[-1]:
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

