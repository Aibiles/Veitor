#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>

int is_multibyte_char(const char *str, int index) {
    // 检查当前字符是否为多字节字符（超过 2 个字节）
    if ((str[index] & 0xF0) == 0xF0) {  // 检查前 4 位是否为 11110
        return 4;
    } else if ((str[index] & 0xE0) == 0xE0) {  // 检查前 3 位是否为 1110
        return 3;
    } else if ((str[index] & 0xC0) == 0xC0) {  // 检查前 2 位是否为 110
        return 2;
    }
    return 1;
}

int main() {
    // 设置本地化环境以支持多字节字符
    setlocale(LC_ALL, "");

    // 定义窄字符字符串
    char *str = "你hao👋おは\t";

    // 计算字符串的字符数
    int length = 0;
    int i = 0;
    while (str[i] != '\0') {
        int char_length = is_multibyte_char(str, i);
        if (char_length > 2) {
            printf("Character at index %d is a multibyte character (length: %d)\n", i, char_length);
        } else {
            printf("Character at index %d is not a multibyte character (length: %d)\n", i, char_length);
        }
        i += char_length;
        length++;
    }

    // 输出结果
    printf("The string is: %s\n", str);
    printf("Number of characters in the string: %d\n", length);

    return 0;
}
