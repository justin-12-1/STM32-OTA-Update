#ifndef __KEY_H
#define __KEY_H

/* 4x4矩阵键盘
 * 行: PB0~PB3 上拉输入 (CubeMX已配置)
 * 列: PB4~PB7 推挽输出 (CubeMX已配置)
 * 注: 标准库中的JTAG引脚重映射由CubeMX的 SYS->Debug: Serial Wire 自动完成
 */
void key16_init(void);
int  read_key16_value(void);

#endif
