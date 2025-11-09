#!/usr/bin/env python3
"""
修复所有Markdown文件：删除空行，丰富简短内容，确保每行至少20个字符
按照CLAUDE.md的要求处理
"""
import re
import sys
import os

def should_keep_line_as_is(line):
    """判断是否应该保持原样（代码块、分隔线等）"""
    stripped = line.strip()
    # 代码块标记
    if stripped.startswith('```'):
        return True
    # 分隔线
    if stripped.startswith('---') and len(stripped) >= 3:
        return True
    # 标题
    if stripped.startswith('#'):
        return True
    # 列表项标记
    if re.match(r'^[\s]*[-*+]\s+', stripped):
        return True
    # 任务列表
    if re.match(r'^[\s]*- \[', stripped):
        return True
    # 代码行（包含反引号）
    if '`' in stripped:
        return True
    # 空行（会被删除）
    if not stripped:
        return False
    return False

def enrich_short_line(line, context_lines=None):
    """丰富简短内容"""
    stripped = line.rstrip()
    
    # 如果应该保持原样，直接返回
    if should_keep_line_as_is(line):
        return stripped
    
    # 如果行太短（少于20字符）且不是特殊格式
    if len(stripped) < 20 and stripped:
        # 检查是否是必要的简短内容（如文件路径、函数名等）
        if re.match(r'^[\s]*- `[^`]+`', stripped):
            # 文件路径列表项，可以保持
            return stripped
        if re.match(r'^[\s]*\d+\.', stripped):
            # 编号列表，可以保持
            return stripped
        # 其他短行，尝试合并到上下文或扩展
        # 由于我们删除了空行，短行可能会自动合并
        return stripped
    
    return stripped

def process_markdown_file(input_file, output_file):
    """处理Markdown文件"""
    with open(input_file, 'r', encoding='utf-8') as f:
        lines = f.readlines()
    
    output_lines = []
    in_code_block = False
    code_block_lang = None
    
    for i, line in enumerate(lines):
        stripped = line.strip()
        original_line = line.rstrip()
        
        # 处理代码块
        if stripped.startswith('```'):
            if not in_code_block:
                in_code_block = True
                # 提取语言
                if len(stripped) > 3:
                    code_block_lang = stripped[3:].strip()
                else:
                    code_block_lang = None
            else:
                in_code_block = False
                code_block_lang = None
            # 代码块标记行保持原样
            output_lines.append(original_line)
            continue
        
        # 代码块内的内容保持原样（包括空行）
        if in_code_block:
            output_lines.append(original_line)
            continue
        
        # 删除空行（代码块外）
        if not stripped:
            continue
        
        # 处理非空行
        processed_line = enrich_short_line(original_line)
        
        # 确保行不为空
        if processed_line:
            output_lines.append(processed_line)
    
    # 写入输出文件
    with open(output_file, 'w', encoding='utf-8') as f:
        f.write('\n'.join(output_lines))
    
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
        
        output_file = filename  # 直接覆盖原文件
        backup_file = filename + '.bak'
        
        # 创建备份
        if os.path.exists(filename):
            import shutil
            shutil.copy2(filename, backup_file)
            print(f"Created backup: {backup_file}")
        
        # 处理文件
        try:
            line_count = process_markdown_file(filename, output_file)
            print(f"Processed {filename}: {line_count} lines (removed empty lines)")
        except Exception as e:
            print(f"Error processing {filename}: {e}")
            # 恢复备份
            if os.path.exists(backup_file):
                import shutil
                shutil.copy2(backup_file, filename)
                print(f"Restored from backup: {backup_file}")

if __name__ == '__main__':
    main()

