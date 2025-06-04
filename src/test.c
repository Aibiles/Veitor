#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>

int is_multibyte_char(const char ch) {
    // 检查当前字符是否为多字节字符（超过 2 个字节）
    if ((ch & 0xF0) == 0xF0) {  // 检查前 4 位是否为 11110
        return 4;
    } else if ((ch & 0xE0) == 0xE0) {  // 检查前 3 位是否为 1110
        return 3;
    } else if ((ch & 0xC0) == 0xC0) {  // 检查前 2 位是否为 110
        return 2;
    }
    return 1;
}

int main() {
    // 设置本地化环境以支持多字节字符
    setlocale(LC_ALL, "");

    // 定义窄字符字符串
    char ch = 'a';
    char test = (ch & 0x1f);
    // 计算字符串的字符数

    // 输出结果
    printf("The string is: %c\n", ch);
    printf("Number of characters in the string: %d\n", is_multibyte_char(ch));

    return 0;
}
